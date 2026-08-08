// Functions referenced by the current upstream UI but accidentally commented
// out in its v1.4 sketch. Preferences avoids the fixed-size EEPROM structure.
void write_eeprom() {
  preferences.begin("my_data", false);
  preferences.putString("req", requestUrl);
  preferences.putInt("inv", iKeuze);
  preferences.end();
}

void read_eeprom() {
  preferences.begin("my_data", true);
  preferences.getString("req", requestUrl, sizeof(requestUrl));
  iKeuze = preferences.getInt("inv", 0);
  preferences.end();
}
