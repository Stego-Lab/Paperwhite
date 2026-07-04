/**
 * @file batt_functions.cpp
 * @author W.Zelinka (OE3WAS, https://github.com/karamo)
 * @brief
 * @version 0.6
 * @date 2026-06-09
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "batt_functions.h"

#if defined(USE_NEW_BATT)

#ifdef USE_BATT

float max_batt = BAT_MAX_VOLTAGE;  //alt
float fBattMax = BAT_MAX_VOLTAGE;  //später extern

static bool firstReading = true;
float rawVoltage;
float BatVoltage;
static float filteredVoltage = 0.0f;
const float alpha = 0.05f;  // Glaettungsfaktor (0.05 = träger, 0.2 = schneller)
unsigned long batt_show_timer = 0;
int BATTshowtime;
#define CDcount 6
static int CountDown = CDcount;

#if defined(WP_DISP_PREVIEW)
// Preview-only: Glitch-Filter (zweite Glaettung mit +/-2%-Ausreisserschutz) zum Testen.
static float GlitchFilteredVoltage = 0.0f;
static bool firstGlitch = false;
#endif

// wird hier nicht verwendet, aber definiert, aber nicht freigegeben
float global_batt = 0;  // in mV
int global_proz = 0;
unsigned long BattTimeWait = 0;

//TODO: ev. weitere Definitionen für spezielle Boards ???
//...

#endif


void check_efuse(void)
{ 	// NOT TESTED, wird nicht benötigt
  printlndeb("[INIT]...efuse not used");
}


void VextON(void)
{
	#if defined(BOARD_WIRELESS_PAPER)
		pinMode(VEXT_ENABLE,OUTPUT);
		digitalWrite(VEXT_ENABLE, HIGH);
	#endif
	#if defined(BOARD_E290)
		pinMode(VEXT_ENABLE_1,OUTPUT);
		digitalWrite(VEXT_ENABLE_1, HIGH);
		pinMode(VEXT_ENABLE_2,OUTPUT);
		digitalWrite(VEXT_ENABLE_2, HIGH);
	#endif
}

void VextOFF(void)  // Vext default OFF
{
	#if defined(BOARD_WIRELESS_PAPER)
		pinMode(VEXT_ENABLE,OUTPUT);
		digitalWrite(VEXT_ENABLE, LOW);
	#endif
	#if defined(BOARD_E290)
		pinMode(VEXT_ENABLE_1,OUTPUT);
		digitalWrite(VEXT_ENABLE_1, LOW);
		pinMode(VEXT_ENABLE_2,OUTPUT);
		digitalWrite(VEXT_ENABLE_2, LOW);
	#endif
}

void ADC_BATT_ON(void)
{
	#if defined(ADC_CTRL_PIN)
		pinMode(ADC_CTRL_PIN, OUTPUT);
		//Heltec V3.1 --- hat keine eigene variants !?!?
		#if defined(BOARD_HELTEC_V31) || defined(BOARD_WIRELESS_PAPER)
			digitalWrite(ADC_CTRL_PIN,LOW);   // active LOW: LOW = Teiler durchgeschaltet/messen
		#else
			digitalWrite(ADC_CTRL_PIN, HIGH);   // E213/E290: active HIGH (am Geraet verifiziert: LOW->0mV, HIGH->840mV)
		#endif
	#endif
}

void ADC_BATT_OFF(void)
{
	#if defined(ADC_CTRL_PIN)
		pinMode(ADC_CTRL_PIN, OUTPUT);
		//Heltec V3.1 --- hat keine eigene variants !?!?
		#if defined(BOARD_HELTEC_V31) || defined(BOARD_WIRELESS_PAPER)
			digitalWrite(ADC_CTRL_PIN,HIGH);
		#else
			digitalWrite(ADC_CTRL_PIN, LOW);   // E213/E290: active HIGH -> OFF = LOW
		#endif
	#endif
}


/**
 * @brief Initialize the battery analog input
 *
 */
void init_batt(void)
{
	#ifdef USE_BATT
		printlndeb("[INIT]...init_batt");
		firstReading = true;
		#if defined(WP_DISP_PREVIEW)
		firstGlitch = false;
		#endif

		// nach Änderung durch Befehl in command_functions.cpp muss init_batt() aufgerufen werden!
		BATTshowtime = (int)meshcom_settings.node_analog_batt_faktor / 1000;  // [--batt factor 99xxx.xxx]
		fBattFaktor = meshcom_settings.node_analog_batt_faktor - BATTshowtime*1000;  // [--batt factor x.xxx]
		if (fBattFaktor == 0.0) { fBattFaktor = 1.0; }
		if (BATTshowtime == 0) { BATTshowtime = 10; }
		fBattMax = meshcom_settings.node_maxv;  // [--maxv x.xxx]
		// -----

		//analogSetPinAttenuation(BAT_VOLT_PIN, ADC_11db);  // alternative Variante
		analogSetAttenuation(BAT_ATTEN);
		analogReadResolution(BAT_WIDTH);

		ADC_BATT_ON();

		#if defined(BOARD_TBEAM) || defined(BOARD_SX1262) || defined(BOARD_SX1268)
		// XPOWERS_CHIP_AXP192 via I2C
		#endif

	#endif  // USE_BATT

	// allgemeine andere Aktionen

	#if defined(BOARD_E290)
		VextON();
	#endif

	// für Display am HELTEC V3/V4 und V3.2 --- gehört nicht unbedingt hier her
	#if defined(BOARD_HELTEC_V3) || defined(BOARD_HELTEC_V4) || defined(BOARD_STICK_V3)
		pinMode(36,OUTPUT);
		digitalWrite(36, LOW);
	#endif

	#if defined(BOARD_TLORA_OLV216)
		pinMode(23, OUTPUT);  // = LORA RESET - gehört nicht unbedingt hier her
	#endif

}  // init_batt



/**
 * @brief Read the analog value from the battery analog pin
 * and convert it to milli volt
 *
 * @return float Battery level in milli volts 0 ... 4200
 */
#if defined(WP_DISP)
// ----- "AKKU LOW"-Beobachtung (WP + E213-Preview, gemeinsamer 2.13"-Display-Pfad) -----
// Ringpuffer der letzten Spannungs-Rohwerte. read_batt() laeuft hier mit 2x/Sekunde -> 12 Werte = 6 s.
// bWpAkkuLow wird vor dem Low-Voltage-Deepsleep gesetzt; das WP-Display zeigt dann statt blank
// "AKKU LOW" + diese Werte (E-Ink haelt das Bild auch im Schlaf -> ablesbar). Die Hysterese
// (erst nach mehreren Low-Messungen schlafen) macht 0.6 selbst via CountDown.
// WP_VHIST_MAX ist zentral in batt_functions.h definiert (auch vom Anzeige-Aufrufer genutzt).
static float wpVHist[WP_VHIST_MAX];
static int   wpVHistCount = 0;
static int   wpVHistHead  = 0;
bool bWpAkkuLow = false;
static void wpPushVolt(float v)
{
    wpVHist[wpVHistHead] = v;
    wpVHistHead = (wpVHistHead + 1) % WP_VHIST_MAX;
    if(wpVHistCount < WP_VHIST_MAX) wpVHistCount++;
}
// Kopiert die letzten Werte NEUESTE ZUERST nach out[], liefert die Anzahl.
int wpBattHistory(float* out, int maxn)
{
    int n = (wpVHistCount < maxn) ? wpVHistCount : maxn;
    for(int i = 0; i < n; i++)
        out[i] = wpVHist[(wpVHistHead - 1 - i + 2 * WP_VHIST_MAX) % WP_VHIST_MAX];
    return n;
}
#if defined(WP_DISP_PREVIEW)
// TK4/K3: grobe Lade-Erkennung ohne VBUS-Pin (die WP hat keinen). Vergleicht das Mittel der
// 3 neuesten mit dem der 3 aeltesten Rohspannungen im ~5-s-Ringpuffer. Steigt die Spannung
// erkennbar an (> 30 mV), wird der Akku geladen -> der Low-Voltage-Deepsleep wird unterdrueckt.
// Hintergrund: firstReading seedet den Filter nach jedem Boot auf fBattMax und unterschreitet
// die Schwelle dann erneut -> ohne diese Pruefung Boot-Deepsleep-Schleife bei leerem, aber
// gerade ladendem Akku. Solange der Puffer noch nicht voll ist (erste ~5 s nach Boot), gilt
// vorsichtshalber "koennte laden" -> kein voreiliger Deepsleep direkt nach dem Boot.
static bool wpBattIsCharging(void)
{
    float h[WP_VHIST_MAX];
    int n = wpBattHistory(h, WP_VHIST_MAX);   // neueste zuerst
    if(n < WP_VHIST_MAX) return true;         // Puffer noch nicht voll -> Boot-Gnadenfrist
    float newest = (h[0] + h[1] + h[2]) / 3.0f;
    float oldest = (h[n - 1] + h[n - 2] + h[n - 3]) / 3.0f;
    return (newest - oldest) > 0.03f;          // > 30 mV Anstieg ueber ~5 s -> laedt
}
#endif
#endif

float read_batt(void)
{
	#ifdef USE_BATT

		// ist hier nicht redundant, da nach deepsleep ein sicherer Platz zum reaktivieren
		ADC_BATT_ON();

		// Messparameter aufbereiten
		// fBattFaktor = Parameter aus Flash
		// fBattMax = Parameter aus Flash
		BATTshowtime = (int)meshcom_settings.node_analog_batt_faktor / 1000;  // [--batt factor 99xxx.xxx]
		fBattFaktor = meshcom_settings.node_analog_batt_faktor - BATTshowtime*1000;  // [--batt factor x.xxx]
		if (fBattFaktor == 0.0) { fBattFaktor = 1.0; }
		if (BATTshowtime == 0) { BATTshowtime = 10; }  // default 10s
		fBattMax = meshcom_settings.node_maxv;  // [--maxv x.xxx]

		// Spezialbehandlung ungetestet
		#if defined(BOARD_HELTEC_T114) || defined(BOARD_T_ECHO) || defined(NRF52_SERIES)
			analogReference(AR_INTERNAL_3_0); // Set the analog reference to 3.0V (default = 3.6V)
			delay(5);
			analogSampleTime(10);	// Set the sampling time to 10us
		#endif

		// einfache Filterfunktion: exponentielle Glättung 1. Ordnung
		rawVoltage = (float)analogReadMilliVolts(BAT_VOLT_PIN)*BAT_MULTIPLIER/1000.0 * fBattFaktor + BAT_VOLT_OFFSET;
		if (firstReading) { filteredVoltage = fBattMax; } // verhindert deepsleep nach REBOOT
		else { filteredVoltage = alpha * rawVoltage + (1.0f - alpha) * filteredVoltage; }

		#if defined(WP_DISP_PREVIEW)
		// Preview-only Filterfunktion: exponentielle Glättung 1. Ordnung mit Glitch-Filter
		if (firstReading) { GlitchFilteredVoltage = fBattMax; } // verhindert deepsleep nach REBOOT
		else {  // testen, ob Messwert erstmalig ausserhalb des +/-2% Bereiches ist => Glitch
			if ((rawVoltage >= GlitchFilteredVoltage*0.98) && (rawVoltage <= GlitchFilteredVoltage*1.02)) {
				// Wert innerhalb Schranken => verrechnen
				GlitchFilteredVoltage = alpha * rawVoltage + (1.0f - alpha) * GlitchFilteredVoltage;
				firstGlitch = false;
			} else { // ausserhalb Schranken
				if (!firstGlitch) { firstGlitch = true; } // verwerfen & merken als firstGlitch
				else { // verrechnen = nachziehen
					GlitchFilteredVoltage = alpha * rawVoltage + (1.0f - alpha) * GlitchFilteredVoltage;
					firstGlitch = false;
				}
			}
		}
		// end Glitch
		#endif

		firstReading = false;

		#if defined(WP_DISP)
		wpPushVolt(rawVoltage);   // 2x/s -> letzte 10 Rohwerte fuer die "AKKU LOW"-Anzeige
		#endif

		if ((batt_show_timer + (1000 * std::max(1,BATTshowtime))) < millis())  // 1 .. 99s
		{
			batt_show_timer = millis();

			if(bDisplayCont)
			{
				bDEBUGLNG = true; // für den nächsten printfdeb language en/de aktivieren
				#if defined(WP_DISP_PREVIEW)
				printfdeb("[BATT];%s;raw:;%.3f;V;max:;%.2f;V;fact:;%.4f;filt:;%.3f;V;%.0f;%%;%.3f;V;%u\n",
					getTimeString().c_str(), rawVoltage, fBattMax, fBattFaktor, filteredVoltage, mv_to_percent(filteredVoltage*1000.0),
					GlitchFilteredVoltage, firstGlitch ? 1 : 0);
				#else
				printfdeb("[BATT];%s;raw:;%.3f;V;max:;%.2f;V;fact:;%.4f;filt:;%.3f;V;%.0f;%%\n",
					getTimeString().c_str(), rawVoltage, fBattMax, fBattFaktor, filteredVoltage, mv_to_percent(filteredVoltage*1000.0));
				#endif
			}
		}

		#if defined(WP_DISP_PREVIEW)
		//BatVoltage = filteredVoltage;
		BatVoltage = GlitchFilteredVoltage;   // Preview: Glitch-bereinigte Spannung als Akkuwert nutzen
		#else
		BatVoltage = filteredVoltage;
		#endif

		// Board spezifische Modifikation
		#if defined(BOARD_E22)       // TODO: und auch die anderen E22 !!!
			if (BatVoltage < 3.0) { BatVoltage = 0; }	// ADC-Eingang nicht mit Versorgungsspannung verbunden
		#endif

		#if defined(BOARD_TBEAM_1W)
			// T-Beam 1W uses 7.4V 2S-battery (max. 8.1V)
			// USB-Spannung kann nicht gemessen werden, nur die AKKU-Spannung
			if(BatVoltage < 5.0) { BatVoltage = 0; }  // USB
		#endif

		// falls die Akku-Spannung BAT_MIN_VOLTAGE erreicht wird, soll ein --deepsleep erfolgen.
		// Dieses erlaubt es, nach einem händischen RESET zum Aufwecken noch kurz nachzusehen,
		// da sich der Akku auch etwas erholt.
		// E213: Akku-Messung am Geraet verifiziert 2026-06-23 (Faktor 4.9245, ADC_CTRL active HIGH):
		// Voll ~4.14 V, Leer-Cutoff ~3.26 V (unter Last; im Boot ~3.5 V = Last-Sag bei leerem LiPo).
		// Low-voltage-Deepsleep wieder scharf wie bei allen anderen Boards. BAT_MIN_VOLTAGE = 3.3 V
		// loest knapp vor dem 3.26-V-Cutoff aus; der firstReading-Seed (filteredVoltage = fBattMax)
		// verhindert den Boot-Deepsleep beim Laden.
		if ((BatVoltage <= (BAT_MIN_VOLTAGE)) && (BatVoltage > 1.0))  // 6.5V für T-Beam 1W, 3.3V für andere Boards
		{
			CountDown--;
			#if defined(WP_DISP_PREVIEW)
			// TK4/K3: laedt der Akku gerade? Dann CountDown neu armieren, BEVOR der Deepsleep-Block
			// laeuft -> verhindert die Boot-Deepsleep-Schleife bei leerem, aber ladendem Akku.
			// Der Original-Deepsleep-Block darunter bleibt dadurch voellig unveraendert.
			if(CountDown == 0 && wpBattIsCharging())
			{
				printlndeb("[BATT]...low Voltage, aber Spannung steigt (laedt) -> kein Deepsleep");
				CountDown = CDcount;   // neu armieren
			}
			#endif
			if (CountDown == 0) {
				if(bDisplayCont)
				{
					bDEBUGLNG = true; // für den nächsten printfdeb language en/de aktivieren
					#if defined(WP_DISP_PREVIEW)
					printfdeb("[BATT];%s;raw:;%.3f;V;max:;%.2f;V;fact:;%.4f;filt:;%.3f;V;%.0f;%%;%.3f\n",
						getTimeString().c_str(), rawVoltage, fBattMax, fBattFaktor, filteredVoltage, mv_to_percent(filteredVoltage*1000.0),
						GlitchFilteredVoltage);
					#else
					printfdeb("[BATT];%s;raw:;%.3f;V;max:;%.2f;V;fact:;%.4f;filt:;%.3f;V;%.0f;%%\n",
						getTimeString().c_str(), rawVoltage, fBattMax, fBattFaktor, filteredVoltage, mv_to_percent(filteredVoltage*1000.0));
					#endif
				}

				// Abschaltmeldung ausgeben
				printlndeb("[ERR]...low Voltage Accu > goto deepsleep");

				delay(1000); // für Ausgabe ermöglichen !!!

				#if defined(BOARD_T_ECHO)   // = NRF52 --- ungetestet
					digitalWrite(Power_On_Pin, LOW);
					//boardPWROff();  // nrf52_functions
				#else
					ADC_BATT_OFF();
					#if !(defined(WP_DISP_PREVIEW))
					// Andere Boards / Original: Display regulaer ausschalten (persistiert node_sset).
					commandAction((char*)"--display off", isPhoneReady, false);
					#else
					// TK2/K2+K12 (WP_PREVIEW): "--display off" auf E-Ink bewusst WEGLASSEN. Es ist
					// auf dem bistabilen Panel sinnlos und teuer - es persistiert node_sset|=0x0002
					// per save_settings() (Flash-Write bei Tiefstspannung = Korruptionsrisiko) und
					// erzeugt einen zusaetzlichen Voll-Refresh. Das sichtbare Loeschen + "AKKU LOW"
					// macht ohnehin wpShowDeepSleep() im --deepsleep -> genau EIN Voll-Refresh, kein
					// Flash-Write. (Das frueher noetige Boot-Override entfaellt damit fuer diesen Pfad.)
					#endif
					#if defined(WP_DISP)
					bWpAkkuLow = true;   // WP/E213-Display zeigt "AKKU LOW" + letzte Werte statt blank
					#endif
					commandAction((char*)"--deepsleep", isPhoneReady, false);
				#endif
				// Node stopped
			}

		} else {
			CountDown = CDcount; // retrigger
		}

		// wenn keine AKKU am BATT PIN ist immmer 0V aber 100% ausgeben
		if(BatVoltage < 1.0) { BatVoltage = 0; }

		return BatVoltage*1000.0;  // [mV]

	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	//=====================================================================================
	#else
		//---------------------------------------------------------------------------
		#if defined(BOARD_HELTEC_T114)
		//TODO: da das KEINE ESP32 ist, ist eine Spezialbehandlung erforderlich !!!
			// ... analogReadMilliVolts(BAT_VOLT_PIN) ...
			BatVoltage = rawVoltage * 3.589;
		#endif

		//---------------------------------------------------------------------------
		#if defined(BOARD_T_ECHO)
		//TODO: da das KEINE ESP32 ist, ist eine Spezialbehandlung erforderlich !!!
			#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
			#define VBAT_DIVIDER      (0.71275837F)   // 2M + 0.806M voltage divider on VBAT = (2M / (0.806M + 2M))
			#define VBAT_DIVIDER_COMP (1.403F)        // Compensation factor for the VBAT divider
			// Convert the raw value to compensated mv, taking the resistor-divider into account (providing the actual LIPO voltage)
			// ADC range is 0..3000mV and resolution is 12-bit (0..4095)
			BatVoltage =  rawVoltage * VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB;
		#endif
		//---------------------------------------------------------------------------

		return 0.0;

	#endif
}  // read_batt


//=====================================================================================

/**
 * @brief Set the Max Batt object
 * @todo genauso wie fBattFaktor behandeln und in main
 *
 * @param u_max_batt [mV]
 */
void setMaxBatt(float u_max_batt)
{
#ifdef USE_BATT
	max_batt = u_max_batt/1000.0;
	fBattMax = u_max_batt/1000.0; // ev. nach main auslagern
#endif
}


/**
 * @brief Volt => Prozent Umrechnung über lineare Näherung
 * @note max_batt = Parameter aus Flash
 *
 * @param mvolts [mV]
 * @return rproz
 */
float mv_to_percent(float mvolts)
{
#ifdef USE_BATT

	// USB - Versorgung
	if(mvolts < 1000.0) { return 100.0; }

	// fBattMax = Parameter aus Flash
  float rproz = (mvolts/1000.0 - BAT_MIN_VOLTAGE)/(fBattMax - BAT_MIN_VOLTAGE) *100.0;
  if (rproz > 100.0) { rproz = 100.0; }
  if (rproz < 0.0) { rproz = 0.0; }
	return round(rproz);

#else

	return 0.0;

#endif
}

#endif  // USE_NEW_BATT
