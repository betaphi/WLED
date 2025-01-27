#include "wled.h"

/*
 * Serializes and parses the cfg.json and wsec.json settings files, stored in internal FS.
 * The structure of the JSON is not to be considered an official API and may change without notice.
 */

//simple macro for ArduinoJSON's or syntax
#define CJSON(a,b) a = b | a

void getStringFromJson(char* dest, const char* src, size_t len) {
  if (src != nullptr) strlcpy(dest, src, len);
}

void deserializeConfig() {
  bool fromeep = false;
  bool success = deserializeConfigSec();
  if (!success) { //if file does not exist, try reading from EEPROM
    deEEPSettings();
    fromeep = true;
  }

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);

  DEBUG_PRINTLN(F("Reading settings from /cfg.json..."));

  success = readObjectFromFile("/cfg.json", nullptr, &doc);
  if (!success) { //if file does not exist, try reading from EEPROM
    if (!fromeep) deEEPSettings();
    return;
  }

  //int rev_major = doc["rev"][0]; // 1
  //int rev_minor = doc["rev"][1]; // 0

  //long vid = doc[F("vid")]; // 2010020

  JsonObject id = doc["id"];
  getStringFromJson(cmDNS, id[F("mdns")], 33);
  getStringFromJson(serverDescription, id[F("name")], 33);
  getStringFromJson(alexaInvocationName, id[F("inv")], 33);

  JsonObject nw_ins_0 = doc["nw"][F("ins")][0];
  getStringFromJson(clientSSID, nw_ins_0[F("ssid")], 33);
  //int nw_ins_0_pskl = nw_ins_0[F("pskl")];
  //The WiFi PSK is normally not contained in the regular file for security reasons.
  //If it is present however, we will use it
  getStringFromJson(clientPass, nw_ins_0["psk"], 65);

  JsonArray nw_ins_0_ip = nw_ins_0["ip"];
  JsonArray nw_ins_0_gw = nw_ins_0["gw"];
  JsonArray nw_ins_0_sn = nw_ins_0["sn"];

  for (byte i = 0; i < 4; i++) {
    CJSON(staticIP[i], nw_ins_0_ip[i]);
    CJSON(staticGateway[i], nw_ins_0_gw[i]);
    CJSON(staticSubnet[i], nw_ins_0_sn[i]);
  }

  JsonObject ap = doc["ap"];
  getStringFromJson(apSSID, ap[F("ssid")], 33);
  getStringFromJson(apPass, ap["psk"] , 65); //normally not present due to security
  //int ap_pskl = ap[F("pskl")];

  CJSON(apChannel, ap[F("chan")]);
  if (apChannel > 13 || apChannel < 1) apChannel = 1;

  CJSON(apHide, ap[F("hide")]);
  if (apHide > 1) apHide = 1;

  CJSON(apBehavior, ap[F("behav")]);
  
  #ifdef WLED_USE_ETHERNET
  JsonObject ethernet = doc[F("eth")];
  CJSON(ethernetType, ethernet["type"]);
  #endif

  /*
  JsonArray ap_ip = ap["ip"];
  for (byte i = 0; i < 4; i++) {
    apIP[i] = ap_ip;
  }*/

  noWifiSleep = doc[F("wifi")][F("sleep")] | !noWifiSleep; // inverted
  noWifiSleep = !noWifiSleep;
  //int wifi_phy = doc[F("wifi")][F("phy")]; //force phy mode n?

  JsonObject hw = doc[F("hw")];

  // initialize LED pins and lengths prior to other HW
  JsonObject hw_led = hw[F("led")];

  CJSON(ledCount, hw_led[F("total")]);
  if (ledCount > MAX_LEDS) ledCount = MAX_LEDS;

  CJSON(strip.ablMilliampsMax, hw_led[F("maxpwr")]);
  CJSON(strip.milliampsPerLed, hw_led[F("ledma")]);
  CJSON(strip.rgbwMode, hw_led[F("rgbwm")]);

  JsonArray ins = hw_led["ins"];
  uint8_t s = 0; //bus iterator
  strip.isRgbw = false;
  busses.removeAll();
  uint32_t mem = 0;
  for (JsonObject elm : ins) {
    if (s >= WLED_MAX_BUSSES) break;
    uint8_t pins[5] = {255, 255, 255, 255, 255};
    JsonArray pinArr = elm[F("pin")];
    if (pinArr.size() == 0) continue;
    pins[0] = pinArr[0];
    uint8_t i = 0;
    for (int p : pinArr) {
      pins[i] = p;
      i++;
      if (i>4) break;
    }

    uint16_t length = elm[F("len")];
    if (length==0) continue;
    uint8_t colorOrder = (int)elm[F("order")];
    //only use skip from the first strip (this shouldn't have been in ins obj. but remains here for compatibility)
    if (s==0) skipFirstLed = elm[F("skip")];
    uint16_t start = elm[F("start")] | 0;
    if (start >= ledCount) continue;
    //limit length of strip if it would exceed total configured LEDs
    if (start + length > ledCount) length = ledCount - start;
    uint8_t ledType = elm["type"] | TYPE_WS2812_RGB;
    bool reversed = elm["rev"];
    //RGBW mode is enabled if at least one of the strips is RGBW
    strip.isRgbw = (strip.isRgbw || BusManager::isRgbw(ledType));
    s++;
    BusConfig bc = BusConfig(ledType, pins, start, length, colorOrder, reversed);
    mem += busses.memUsage(bc);
    if (mem <= MAX_LED_MEMORY) busses.add(bc);
  }
  strip.finalizeInit(ledCount, skipFirstLed);
  if (hw_led["rev"]) busses.getBus(0)->reversed = true; //set 0.11 global reversed setting for first bus

  JsonObject hw_btn_ins_0 = hw[F("btn")][F("ins")][0];
  CJSON(buttonType, hw_btn_ins_0["type"]);
  int hw_btn_pin = hw_btn_ins_0[F("pin")][0];
  if (pinManager.allocatePin(hw_btn_pin,false)) {
    btnPin = hw_btn_pin;
    pinMode(btnPin, INPUT_PULLUP);
  } else {
    btnPin = -1;
  }

  JsonArray hw_btn_ins_0_macros = hw_btn_ins_0[F("macros")];
  CJSON(macroButton, hw_btn_ins_0_macros[0]);
  CJSON(macroLongPress,hw_btn_ins_0_macros[1]);
  CJSON(macroDoublePress, hw_btn_ins_0_macros[2]);

  //int hw_btn_ins_0_type = hw_btn_ins_0["type"]; // 0

  #ifndef WLED_DISABLE_INFRARED
  int hw_ir_pin = hw["ir"]["pin"] | -1; // 4
  if (pinManager.allocatePin(hw_ir_pin,false)) {
    irPin = hw_ir_pin;
  } else {
    irPin = -1;
  }
  #endif
  CJSON(irEnabled, hw["ir"]["type"]);

  JsonObject relay = hw[F("relay")];

  int hw_relay_pin = relay["pin"];
  if (pinManager.allocatePin(hw_relay_pin,true)) {
    rlyPin = hw_relay_pin;
    pinMode(rlyPin, OUTPUT);
  } else {
    rlyPin = -1;
  }
  if (relay.containsKey("rev")) {
    rlyMde = !relay["rev"];
  }

  //int hw_status_pin = hw[F("status")][F("pin")]; // -1

  JsonObject light = doc[F("light")];
  CJSON(briMultiplier, light[F("scale-bri")]);
  CJSON(strip.paletteBlend, light[F("pal-mode")]);

  float light_gc_bri = light[F("gc")]["bri"];
  float light_gc_col = light[F("gc")]["col"]; // 2.8
  if (light_gc_bri > 1.5) strip.gammaCorrectBri = true;
  else if (light_gc_bri > 0.5) strip.gammaCorrectBri = false;
  if (light_gc_col > 1.5) strip.gammaCorrectCol = true;
  else if (light_gc_col > 0.5) strip.gammaCorrectCol = false;

  JsonObject light_tr = light[F("tr")];
  CJSON(fadeTransition, light_tr[F("mode")]);
  int tdd = light_tr[F("dur")] | -1;
  if (tdd >= 0) transitionDelayDefault = tdd * 100;
  CJSON(strip.paletteFade, light_tr[F("pal")]);

  JsonObject light_nl = light["nl"];
  CJSON(nightlightMode, light_nl[F("mode")]);
  CJSON(nightlightDelayMinsDefault, light_nl[F("dur")]);
  nightlightDelayMins = nightlightDelayMinsDefault;

  CJSON(nightlightTargetBri, light_nl[F("tbri")]);
  CJSON(macroNl, light_nl[F("macro")]);

  JsonObject def = doc[F("def")];
  CJSON(bootPreset, def[F("ps")]);
  CJSON(turnOnAtBoot, def["on"]); // true
  CJSON(briS, def["bri"]); // 128

  JsonObject def_cy = def[F("cy")];
  CJSON(presetCyclingEnabled, def_cy["on"]);

  CJSON(presetCycleMin, def_cy[F("range")][0]);
  CJSON(presetCycleMax, def_cy[F("range")][1]);

  tdd = def_cy[F("dur")] | -1;
  if (tdd > 0) presetCycleTime = tdd;

  JsonObject interfaces = doc["if"];

  JsonObject if_sync = interfaces[F("sync")];
  CJSON(udpPort, if_sync[F("port0")]); // 21324
  CJSON(udpPort2, if_sync[F("port1")]); // 65506

  JsonObject if_sync_recv = if_sync["recv"];
  CJSON(receiveNotificationBrightness, if_sync_recv["bri"]);
  CJSON(receiveNotificationColor, if_sync_recv["col"]);
  CJSON(receiveNotificationEffects, if_sync_recv[F("fx")]);
  receiveNotifications = (receiveNotificationBrightness || receiveNotificationColor || receiveNotificationEffects);

  JsonObject if_sync_send = if_sync["send"];
  CJSON(notifyDirectDefault, if_sync_send[F("dir")]);
  notifyDirect = notifyDirectDefault;
  CJSON(notifyButton, if_sync_send[F("btn")]);
  CJSON(notifyAlexa, if_sync_send[F("va")]);
  CJSON(notifyHue, if_sync_send[F("hue")]);
  CJSON(notifyMacro, if_sync_send[F("macro")]);
  CJSON(notifyTwice, if_sync_send[F("twice")]);

  JsonObject if_nodes = interfaces["nodes"];
  CJSON(nodeListEnabled, if_nodes[F("list")]);
  CJSON(nodeBroadcastEnabled, if_nodes[F("bcast")]);

  JsonObject if_live = interfaces["live"];
  CJSON(receiveDirect, if_live["en"]);
  CJSON(e131Port, if_live["port"]); // 5568
  CJSON(e131Multicast, if_live[F("mc")]);

  JsonObject if_live_dmx = if_live[F("dmx")];
  CJSON(e131Universe, if_live_dmx[F("uni")]);
  CJSON(e131SkipOutOfSequence, if_live_dmx[F("seqskip")]);
  CJSON(DMXAddress, if_live_dmx[F("addr")]);
  CJSON(DMXMode, if_live_dmx[F("mode")]);

  tdd = if_live[F("timeout")] | -1;
  if (tdd >= 0) realtimeTimeoutMs = tdd * 100;
  CJSON(arlsForceMaxBri, if_live[F("maxbri")]);
  CJSON(arlsDisableGammaCorrection, if_live[F("no-gc")]); // false
  CJSON(arlsOffset, if_live[F("offset")]); // 0

  CJSON(alexaEnabled, interfaces[F("va")][F("alexa")]); // false

  CJSON(macroAlexaOn, interfaces[F("va")][F("macros")][0]);
  CJSON(macroAlexaOff, interfaces[F("va")][F("macros")][1]);

  const char* apikey = interfaces["blynk"][F("token")] | "Hidden";
  tdd = strnlen(apikey, 36);
  if (tdd > 20 || tdd == 0)
    getStringFromJson(blynkApiKey, apikey, 36); //normally not present due to security

  JsonObject if_blynk = interfaces["blynk"];
  getStringFromJson(blynkHost, if_blynk[F("host")], 33);
  CJSON(blynkPort, if_blynk["port"]);

  JsonObject if_mqtt = interfaces["mqtt"];
  CJSON(mqttEnabled, if_mqtt["en"]);
  getStringFromJson(mqttServer, if_mqtt[F("broker")], 33);
  CJSON(mqttPort, if_mqtt["port"]); // 1883
  getStringFromJson(mqttUser, if_mqtt[F("user")], 41);
  getStringFromJson(mqttPass, if_mqtt["psk"], 41); //normally not present due to security
  getStringFromJson(mqttClientID, if_mqtt[F("cid")], 41);

  getStringFromJson(mqttDeviceTopic, if_mqtt[F("topics")][F("device")], 33); // "wled/test"
  getStringFromJson(mqttGroupTopic, if_mqtt[F("topics")][F("group")], 33); // ""

  JsonObject if_hue = interfaces[F("hue")];
  CJSON(huePollingEnabled, if_hue["en"]);
  CJSON(huePollLightId, if_hue["id"]);
  tdd = if_hue[F("iv")] | -1;
  if (tdd >= 2) huePollIntervalMs = tdd * 100;

  JsonObject if_hue_recv = if_hue["recv"];
  CJSON(hueApplyOnOff, if_hue_recv["on"]);
  CJSON(hueApplyBri, if_hue_recv["bri"]);
  CJSON(hueApplyColor, if_hue_recv["col"]);

  JsonArray if_hue_ip = if_hue["ip"];

  for (byte i = 0; i < 4; i++)
    CJSON(hueIP[i], if_hue_ip[i]);

  JsonObject if_ntp = interfaces[F("ntp")];
  CJSON(ntpEnabled, if_ntp["en"]);
  getStringFromJson(ntpServerName, if_ntp[F("host")], 33); // "1.wled.pool.ntp.org"
  CJSON(currentTimezone, if_ntp[F("tz")]);
  CJSON(utcOffsetSecs, if_ntp[F("offset")]);
  CJSON(useAMPM, if_ntp[F("ampm")]);

  JsonObject ol = doc[F("ol")];
  CJSON(overlayDefault ,ol[F("clock")]); // 0
  CJSON(countdownMode, ol[F("cntdwn")]);
  overlayCurrent = overlayDefault;

  CJSON(overlayMin, ol[F("min")]);
  CJSON(overlayMax, ol[F("max")]);
  CJSON(analogClock12pixel, ol[F("o12pix")]);
  CJSON(analogClock5MinuteMarks, ol[F("o5m")]);
  CJSON(analogClockSecondsTrail, ol[F("osec")]);

  //timed macro rules
  JsonObject tm = doc[F("timers")];
  JsonObject cntdwn = tm[F("cntdwn")];
  JsonArray cntdwn_goal = cntdwn[F("goal")];
  CJSON(countdownYear,  cntdwn_goal[0]);
  CJSON(countdownMonth, cntdwn_goal[1]);
  CJSON(countdownDay,   cntdwn_goal[2]);
  CJSON(countdownHour,  cntdwn_goal[3]);
  CJSON(countdownMin,   cntdwn_goal[4]);
  CJSON(countdownSec,   cntdwn_goal[5]);
  CJSON(macroCountdown, cntdwn[F("macro")]);
  setCountdown();

  JsonArray timers = tm[F("ins")];
  uint8_t it = 0;
  for (JsonObject timer : timers) {
    if (it > 7) break;
    CJSON(timerHours[it], timer[F("hour")]);
    CJSON(timerMinutes[it], timer[F("min")]);
    CJSON(timerMacro[it], timer[F("macro")]);

    byte dowPrev =  timerWeekday[it];
    //note: act is currently only 0 or 1.
    //the reason we are not using bool is that the on-disk type in 0.11.0 was already int
    int actPrev = timerWeekday[it] & 0x01;
    CJSON(timerWeekday[it], timer[F("dow")]);
    if (timerWeekday[it] != dowPrev) { //present in JSON
      timerWeekday[it] <<= 1; //add active bit
      int act = timer["en"] | actPrev;
      if (act) timerWeekday[it]++;
    }

    it++;
  }

  JsonObject ota = doc["ota"];
  const char* pwd = ota["psk"]; //normally not present due to security

  bool pwdCorrect = !otaLock; //always allow access if ota not locked
  if (pwd != nullptr && strncmp(otaPass, pwd, 33) == 0) pwdCorrect = true;

  if (pwdCorrect) { //only accept these values from cfg.json if ota is unlocked (else from wsec.json)
    CJSON(otaLock, ota[F("lock")]);
    CJSON(wifiLock, ota[F("lock-wifi")]);
    CJSON(aOtaEnabled, ota[F("aota")]);
    getStringFromJson(otaPass, pwd, 33); //normally not present due to security
  }

  #ifdef WLED_ENABLE_DMX
  JsonObject dmx = doc["dmx"];
  CJSON(DMXChannels, dmx[F("chan")]);
  CJSON(DMXGap,dmx[F("gap")]);
  CJSON(DMXStart, dmx[F("start")]);
  CJSON(DMXStartLED,dmx[F("start-led")]);

  JsonArray dmx_fixmap = dmx[F("fixmap")];
  it = 0;
  for (int i : dmx_fixmap) {
    if (it > 14) break;
    CJSON(DMXFixtureMap[i],dmx_fixmap[i]);
    it++;
  }
  #endif

  JsonObject usermods_settings = doc["um"];
  usermods.readFromConfig(usermods_settings);
}

void serializeConfig() {
  serializeConfigSec();

  DEBUG_PRINTLN(F("Writing settings to /cfg.json..."));

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);

  JsonArray rev = doc.createNestedArray("rev");
  rev.add(1); //major settings revision
  rev.add(0); //minor settings revision

  doc[F("vid")] = VERSION;

  JsonObject id = doc.createNestedObject("id");
  id[F("mdns")] = cmDNS;
  id[F("name")] = serverDescription;
  id[F("inv")] = alexaInvocationName;

  JsonObject nw = doc.createNestedObject("nw");

  JsonArray nw_ins = nw.createNestedArray("ins");

  JsonObject nw_ins_0 = nw_ins.createNestedObject();
  nw_ins_0[F("ssid")] = clientSSID;
  nw_ins_0[F("pskl")] = strlen(clientPass);

  JsonArray nw_ins_0_ip = nw_ins_0.createNestedArray("ip");
  JsonArray nw_ins_0_gw = nw_ins_0.createNestedArray("gw");
  JsonArray nw_ins_0_sn = nw_ins_0.createNestedArray("sn");

  for (byte i = 0; i < 4; i++) {
    nw_ins_0_ip.add(staticIP[i]);
    nw_ins_0_gw.add(staticGateway[i]);
    nw_ins_0_sn.add(staticSubnet[i]);
  }

  JsonObject ap = doc.createNestedObject("ap");
  ap[F("ssid")] = apSSID;
  ap[F("pskl")] = strlen(apPass);
  ap[F("chan")] = apChannel;
  ap[F("hide")] = apHide;
  ap[F("behav")] = apBehavior;

  JsonArray ap_ip = ap.createNestedArray("ip");
  ap_ip.add(4);
  ap_ip.add(3);
  ap_ip.add(2);
  ap_ip.add(1);

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi[F("sleep")] = !noWifiSleep;
  wifi[F("phy")] = 1;

  #ifdef WLED_USE_ETHERNET
  JsonObject ethernet = doc.createNestedObject("eth");
  ethernet["type"] = ethernetType;
  #endif

  JsonObject hw = doc.createNestedObject("hw");

  JsonObject hw_led = hw.createNestedObject("led");
  hw_led[F("total")] = ledCount;
  hw_led[F("maxpwr")] = strip.ablMilliampsMax;
  hw_led[F("ledma")] = strip.milliampsPerLed;
  hw_led[F("rgbwm")] = strip.rgbwMode;

  JsonArray hw_led_ins = hw_led.createNestedArray("ins");

  for (uint8_t s = 0; s < busses.getNumBusses(); s++) {
    Bus *bus = busses.getBus(s);
    if (!bus || bus->getLength()==0) break;
    JsonObject ins = hw_led_ins.createNestedObject();
    ins["en"] = true;
    ins[F("start")] = bus->getStart();
    ins[F("len")] = bus->getLength();
    JsonArray ins_pin = ins.createNestedArray("pin");
    uint8_t pins[5];
    uint8_t nPins = bus->getPins(pins);
    for (uint8_t i = 0; i < nPins; i++) ins_pin.add(pins[i]);
    ins[F("order")] = bus->getColorOrder();
    ins["rev"] = bus->reversed;
    ins[F("skip")] = (skipFirstLed && s == 0) ? 1 : 0;
    ins["type"] = bus->getType();
  }

  JsonObject hw_btn = hw.createNestedObject("btn");

  JsonArray hw_btn_ins = hw_btn.createNestedArray("ins");

  // button BTNPIN
  JsonObject hw_btn_ins_0 = hw_btn_ins.createNestedObject();
  hw_btn_ins_0["type"] = buttonType;

  JsonArray hw_btn_ins_0_pin = hw_btn_ins_0.createNestedArray("pin");
  hw_btn_ins_0_pin.add(btnPin);

  JsonArray hw_btn_ins_0_macros = hw_btn_ins_0.createNestedArray("macros");
  hw_btn_ins_0_macros.add(macroButton);
  hw_btn_ins_0_macros.add(macroLongPress);
  hw_btn_ins_0_macros.add(macroDoublePress);

  #ifndef WLED_DISABLE_INFRARED
  JsonObject hw_ir = hw.createNestedObject("ir");
  hw_ir["pin"] = irPin;
  hw_ir[F("type")] = irEnabled;              // the byte 'irEnabled' does contain the IR-Remote Type ( 0=disabled )
  #endif

  JsonObject hw_relay = hw.createNestedObject(F("relay"));
  hw_relay["pin"] = rlyPin;
  hw_relay["rev"] = !rlyMde;

  //JsonObject hw_status = hw.createNestedObject("status");
  //hw_status["pin"] = -1;

  JsonObject light = doc.createNestedObject(F("light"));
  light[F("scale-bri")] = briMultiplier;
  light[F("pal-mode")] = strip.paletteBlend;

  JsonObject light_gc = light.createNestedObject("gc");
  light_gc["bri"] = (strip.gammaCorrectBri) ? 2.8 : 1.0;
  light_gc["col"] = (strip.gammaCorrectCol) ? 2.8 : 1.0;

  JsonObject light_tr = light.createNestedObject("tr");
  light_tr[F("mode")] = fadeTransition;
  light_tr[F("dur")] = transitionDelayDefault / 100;
  light_tr[F("pal")] = strip.paletteFade;

  JsonObject light_nl = light.createNestedObject("nl");
  light_nl[F("mode")] = nightlightMode;
  light_nl[F("dur")] = nightlightDelayMinsDefault;
  light_nl[F("tbri")] = nightlightTargetBri;
  light_nl[F("macro")] = macroNl;

  JsonObject def = doc.createNestedObject("def");
  def[F("ps")] = bootPreset;
  def["on"] = turnOnAtBoot;
  def["bri"] = briS;

  //to be removed once preset cycles are presets
  if (saveCurrPresetCycConf) {
    JsonObject def_cy = def.createNestedObject("cy");
    def_cy["on"] = presetCyclingEnabled;

    JsonArray def_cy_range = def_cy.createNestedArray(F("range"));
    def_cy_range.add(presetCycleMin);
    def_cy_range.add(presetCycleMax);
    def_cy[F("dur")] = presetCycleTime;
  }

  JsonObject interfaces = doc.createNestedObject("if");

  JsonObject if_sync = interfaces.createNestedObject("sync");
  if_sync[F("port0")] = udpPort;
  if_sync[F("port1")] = udpPort2;

  JsonObject if_sync_recv = if_sync.createNestedObject("recv");
  if_sync_recv["bri"] = receiveNotificationBrightness;
  if_sync_recv["col"] = receiveNotificationColor;
  if_sync_recv[F("fx")] = receiveNotificationEffects;

  JsonObject if_sync_send = if_sync.createNestedObject("send");
  if_sync_send[F("dir")] = notifyDirect;
  if_sync_send[F("btn")] = notifyButton;
  if_sync_send[F("va")] = notifyAlexa;
  if_sync_send[F("hue")] = notifyHue;
  if_sync_send[F("macro")] = notifyMacro;
  if_sync_send[F("twice")] = notifyTwice;

  JsonObject if_nodes = interfaces.createNestedObject("nodes");
  if_nodes[F("list")] = nodeListEnabled;
  if_nodes[F("bcast")] = nodeBroadcastEnabled;

  JsonObject if_live = interfaces.createNestedObject("live");
  if_live["en"] = receiveDirect;
  if_live["port"] = e131Port;
  if_live[F("mc")] = e131Multicast;

  JsonObject if_live_dmx = if_live.createNestedObject("dmx");
  if_live_dmx[F("uni")] = e131Universe;
  if_live_dmx[F("seqskip")] = e131SkipOutOfSequence;
  if_live_dmx[F("addr")] = DMXAddress;
  if_live_dmx[F("mode")] = DMXMode;
  if_live[F("timeout")] = realtimeTimeoutMs / 100;
  if_live[F("maxbri")] = arlsForceMaxBri;
  if_live[F("no-gc")] = arlsDisableGammaCorrection;
  if_live[F("offset")] = arlsOffset;

  JsonObject if_va = interfaces.createNestedObject("va");
  if_va[F("alexa")] = alexaEnabled;

  JsonArray if_va_macros = if_va.createNestedArray("macros");
  if_va_macros.add(macroAlexaOn);
  if_va_macros.add(macroAlexaOff);
  JsonObject if_blynk = interfaces.createNestedObject("blynk");
  if_blynk[F("token")] = strlen(blynkApiKey) ? "Hidden":"";
  if_blynk[F("host")] = blynkHost;
  if_blynk["port"] = blynkPort;

  JsonObject if_mqtt = interfaces.createNestedObject("mqtt");
  if_mqtt["en"] = mqttEnabled;
  if_mqtt[F("broker")] = mqttServer;
  if_mqtt["port"] = mqttPort;
  if_mqtt[F("user")] = mqttUser;
  if_mqtt[F("pskl")] = strlen(mqttPass);
  if_mqtt[F("cid")] = mqttClientID;

  JsonObject if_mqtt_topics = if_mqtt.createNestedObject(F("topics"));
  if_mqtt_topics[F("device")] = mqttDeviceTopic;
  if_mqtt_topics[F("group")] = mqttGroupTopic;

  JsonObject if_hue = interfaces.createNestedObject("hue");
  if_hue["en"] = huePollingEnabled;
  if_hue["id"] = huePollLightId;
  if_hue[F("iv")] = huePollIntervalMs / 100;

  JsonObject if_hue_recv = if_hue.createNestedObject("recv");
  if_hue_recv["on"] = hueApplyOnOff;
  if_hue_recv["bri"] = hueApplyBri;
  if_hue_recv["col"] = hueApplyColor;

  JsonArray if_hue_ip = if_hue.createNestedArray("ip");
  for (byte i = 0; i < 4; i++) {
    if_hue_ip.add(hueIP[i]);
  }

  JsonObject if_ntp = interfaces.createNestedObject("ntp");
  if_ntp["en"] = ntpEnabled;
  if_ntp[F("host")] = ntpServerName;
  if_ntp[F("tz")] = currentTimezone;
  if_ntp[F("offset")] = utcOffsetSecs;
  if_ntp[F("ampm")] = useAMPM;

  JsonObject ol = doc.createNestedObject("ol");
  ol[F("clock")] = overlayDefault;
  ol[F("cntdwn")] = countdownMode;

  ol[F("min")] = overlayMin;
  ol[F("max")] = overlayMax;
  ol[F("o12pix")] = analogClock12pixel;
  ol[F("o5m")] = analogClock5MinuteMarks;
  ol[F("osec")] = analogClockSecondsTrail;

  JsonObject timers = doc.createNestedObject(F("timers"));

  JsonObject cntdwn = timers.createNestedObject(F("cntdwn"));
  JsonArray goal = cntdwn.createNestedArray(F("goal"));
  goal.add(countdownYear); goal.add(countdownMonth); goal.add(countdownDay);
  goal.add(countdownHour); goal.add(countdownMin); goal.add(countdownSec);
  cntdwn[F("macro")] = macroCountdown;

  JsonArray timers_ins = timers.createNestedArray("ins");

  for (byte i = 0; i < 8; i++) {
    if (timerMacro[i] == 0 && timerHours[i] == 0 && timerMinutes[i] == 0) continue;
    JsonObject timers_ins0 = timers_ins.createNestedObject();
    timers_ins0["en"] = (timerWeekday[i] & 0x01);
    timers_ins0[F("hour")] = timerHours[i];
    timers_ins0[F("min")] = timerMinutes[i];
    timers_ins0[F("macro")] = timerMacro[i];
    timers_ins0[F("dow")] = timerWeekday[i] >> 1;
  }

  JsonObject ota = doc.createNestedObject("ota");
  ota[F("lock")] = otaLock;
  ota[F("lock-wifi")] = wifiLock;
  ota[F("pskl")] = strlen(otaPass);
  ota[F("aota")] = aOtaEnabled;

  #ifdef WLED_ENABLE_DMX
  JsonObject dmx = doc.createNestedObject("dmx");
  dmx[F("chan")] = DMXChannels;
  dmx[F("gap")] = DMXGap;
  dmx[F("start")] = DMXStart;
  dmx[F("start-led")] = DMXStartLED;

  JsonArray dmx_fixmap = dmx.createNestedArray(F("fixmap"));
  for (byte i = 0; i < 15; i++)
    dmx_fixmap.add(DMXFixtureMap[i]);
  #endif
  //}

  JsonObject usermods_settings = doc.createNestedObject("um");
  usermods.addToConfig(usermods_settings);

  File f = WLED_FS.open("/cfg.json", "w");
  if (f) serializeJson(doc, f);
  f.close();
}

//settings in /wsec.json, not accessible via webserver, for passwords and tokens
bool deserializeConfigSec() {
  DEBUG_PRINTLN(F("Reading settings from /wsec.json..."));

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);

  bool success = readObjectFromFile("/wsec.json", nullptr, &doc);
  if (!success) return false;

  JsonObject nw_ins_0 = doc["nw"][F("ins")][0];
  getStringFromJson(clientPass, nw_ins_0["psk"], 65);

  JsonObject ap = doc["ap"];
  getStringFromJson(apPass, ap["psk"] , 65);

  JsonObject interfaces = doc["if"];

  const char* apikey = interfaces["blynk"][F("token")] | "Hidden";
  int tdd = strnlen(apikey, 36);
  if (tdd > 20 || tdd == 0)
    getStringFromJson(blynkApiKey, apikey, 36);

  JsonObject if_mqtt = interfaces["mqtt"];
  getStringFromJson(mqttPass, if_mqtt["psk"], 41);

  getStringFromJson(hueApiKey, interfaces[F("hue")][F("key")], 47);

  JsonObject ota = doc["ota"];
  getStringFromJson(otaPass, ota[F("pwd")], 33);
  CJSON(otaLock, ota[F("lock")]);
  CJSON(wifiLock, ota[F("lock-wifi")]);
  CJSON(aOtaEnabled, ota[F("aota")]);

  return true;
}

void serializeConfigSec() {
  DEBUG_PRINTLN(F("Writing settings to /wsec.json..."));

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);

  JsonObject nw = doc.createNestedObject("nw");

  JsonArray nw_ins = nw.createNestedArray("ins");

  JsonObject nw_ins_0 = nw_ins.createNestedObject();
  nw_ins_0["psk"] = clientPass;

  JsonObject ap = doc.createNestedObject("ap");
  ap["psk"] = apPass;

  JsonObject interfaces = doc.createNestedObject("if");
  JsonObject if_blynk = interfaces.createNestedObject("blynk");
  if_blynk[F("token")] = blynkApiKey;
  JsonObject if_mqtt = interfaces.createNestedObject("mqtt");
  if_mqtt["psk"] = mqttPass;
  JsonObject if_hue = interfaces.createNestedObject("hue");
  if_hue[F("key")] = hueApiKey;

  JsonObject ota = doc.createNestedObject("ota");
  ota[F("pwd")] = otaPass;
  ota[F("lock")] = otaLock;
  ota[F("lock-wifi")] = wifiLock;
  ota[F("aota")] = aOtaEnabled;

  File f = WLED_FS.open("/wsec.json", "w");
  if (f) serializeJson(doc, f);
  f.close();
}
