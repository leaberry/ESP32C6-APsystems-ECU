#include <nvs.h>

static const char ECU_DEFAULT_ID[] = "D8A3011B9780";
static const char ECU_CONFIG_PATH[] = "/basisconfig.json";
static const char ECU_CONFIG_TEMP[] = "/basisconfig.identity.tmp";
static const char ECU_CONFIG_OLD[] = "/basisconfig.identity.old";

bool ecuIdentityRecoverConfig() {
  // SPIFFS cannot replace an existing file with rename. Recover the old file
  // if power failed between renaming it and activating the staged new file.
  return SPIFFS.exists(ECU_CONFIG_PATH) || !SPIFFS.exists(ECU_CONFIG_OLD) ||
      SPIFFS.rename(ECU_CONFIG_OLD, ECU_CONFIG_PATH);
}

bool ecuIdentityHasPairing() {
  nvs_iterator_t entries = nullptr;
  esp_err_t result = nvs_entry_find("nvs", "apsradio", NVS_TYPE_ANY, &entries);
  nvs_release_iterator(entries);
  // Any learned peer, including an orphan, or a storage error blocks rotation.
  if (result != ESP_ERR_NVS_NOT_FOUND) return true;
  for (int i = 0; i < 9; ++i) {
    char path[40];
    for (const char *suffix : {"", ".pair", ".pair-old"}) {
      snprintf(path, sizeof(path), "/Inv_Prop%d.str%s", i, suffix);
      if (!SPIFFS.exists(path)) continue;
      File file = SPIFFS.open(path, "r");
      inverters saved;
      bool readable = file && file.size() == sizeof(saved) &&
          file.read((uint8_t *)&saved, sizeof(saved)) == sizeof(saved);
      file.close();
      // Only a complete, explicitly unpaired record is safe. Treat legacy
      // pending markers and malformed records as possible pairing evidence.
      if (!readable || memcmp(saved.invID, "0000", sizeof(saved.invID))) return true;
    }
  }
  return false;
}

void ecuIdentityGenerate(char id[13]) {
  // Not a secret: the existing ESP32 random source is sufficient. The first
  // two bytes also become the PAN, so exclude reserved 0000 and FFFF values.
  unsigned prefix = 1U + esp_random() % 65534U;
  snprintf(id, 13, "%04X%08lX", prefix, (unsigned long)esp_random());
  if (!strcmp(id, ECU_DEFAULT_ID)) id[11] = '1';
}

bool ecuIdentitySave(const char *id) {
  if (!ecuIdentityRecoverConfig()) return false;
  JsonDocument doc;
  bool hadFile = SPIFFS.exists(ECU_CONFIG_PATH);
  if (hadFile) {
    File file = SPIFFS.open(ECU_CONFIG_PATH, "r");
    if (!file) return false;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return false;
  } else {
    basisConfigDocument(doc);
  }
  doc["ECU_ID"] = id; // Preserve all other settings, including unknown keys.
  File staged = SPIFFS.open(ECU_CONFIG_TEMP, "w");
  if (!staged) return false;
  bool written = serializeJson(doc, staged) == measureJson(doc);
  staged.flush();
  staged.close();
  if (!written) { SPIFFS.remove(ECU_CONFIG_TEMP); return false; }
  // Read back before committing the identity to RAM or the live config.
  staged = SPIFFS.open(ECU_CONFIG_TEMP, "r");
  JsonDocument verify;
  bool verified = staged && !deserializeJson(verify, staged) &&
      !strcmp(verify["ECU_ID"] | "", id);
  staged.close();
  if (!verified) { SPIFFS.remove(ECU_CONFIG_TEMP); return false; }
  if (hadFile && ((SPIFFS.exists(ECU_CONFIG_OLD) && !SPIFFS.remove(ECU_CONFIG_OLD)) ||
                  !SPIFFS.rename(ECU_CONFIG_PATH, ECU_CONFIG_OLD))) return false;
  if (!SPIFFS.rename(ECU_CONFIG_TEMP, ECU_CONFIG_PATH)) {
    if (hadFile) SPIFFS.rename(ECU_CONFIG_OLD, ECU_CONFIG_PATH);
    return false;
  }
  if (hadFile) SPIFFS.remove(ECU_CONFIG_OLD);
  return true;
}

void ecuIdentityBegin() {
  if (strcasecmp(ECU_ID, ECU_DEFAULT_ID)) return;
  if (ecuIdentityHasPairing()) {
    Serial.println(F("ECU ID: keeping default because pairing data exists or could not be checked"));
    return;
  }
  char generated[13];
  ecuIdentityGenerate(generated);
  if (!ecuIdentitySave(generated)) {
    Serial.println(F("ECU ID: save failed; keeping current identifier"));
    return;
  }
  strlcpy(ECU_ID, generated, sizeof(ECU_ID));
  Serial.print(F("ECU ID: generated and saved "));
  Serial.println(ECU_ID);
}
