# Appendiks C: Ordliste

[← Appendiks B: REST API-reference](B_REST_API_Reference.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks D: ST Logic Funktionsreference →](D_ST_Logic_Funktionsreference.md)

---

## Modbus-termer

| Term | Forklaring |
|------|------------|
| **Modbus RTU** | Seriel udgave af Modbus-protokollen, kører over RS-485. Binært framing med CRC16-checksum. |
| **Modbus TCP** | Modbus over Ethernet/IP-netværk — samme function code-baserede PDU som RTU, men indrammet i en MBAP-header i stedet for adresse+CRC16. Systemets egen Slave/Master (RS-485) taler RTU, men PLC'en taler Modbus TCP som klient mod eksterne Modbus Expansion Boards (se nedenfor og [§6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409)). |
| **MBAP-header** | Modbus Application Protocol-header — de 7 byte (Transaction ID, Protocol ID, Length, Unit ID) der indrammer en Modbus TCP-PDU. Unit ID bærer den fysiske RTU slave-adresse videre gennem en TCP-gateway/expansion-board. |
| **Modbus Expansion Board** | Et separat, fysisk "HypervisionPLC Extension Board" med egne RS485/RS232-kanaler (A/B), administreret af PLC'en over Modbus TCP + et REST-management-API. Se [§6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409). |
| **Slave / Master** | I Modbus-terminologi: Slaven svarer passivt på forespørgsler; Masteren initierer dem. Systemet kan spille begge roller (se [kapitel 6](06_Modbus_Interface.md)). |
| **Function Code (FC)** | Nummereret operationstype i en Modbus-forespørgsel (fx FC03 = læs holding-registre). Se [§6.2](06_Modbus_Interface.md#62-understøttede-function-codes). |
| **Holding Register** | 16-bit læs/skriv-register. Den mest almindeligt anvendte datatype i Modbus. |
| **Input Register** | 16-bit **kun-læs**-register — typisk brugt til sensordata fra en enheds perspektiv. |
| **Coil** | 1-bit læs/skriv-værdi (svarer til en digital udgang/relæ). |
| **Discrete Input** | 1-bit **kun-læs**-værdi (svarer til en digital indgang). |
| **Slave-ID** | Adresse (1–247) der identificerer én enhed på en delt RS-485-bus. 0 er reserveret til broadcast. |
| **CRC16** | Checksum-algoritme brugt til at opdage transmissionsfejl i Modbus RTU-frames. |
| **RS-485** | Den fysiske to-leder-standard Modbus RTU typisk kører over — tillader flere enheder på samme delte bus. |
| **DE/RE** | Driver Enable / Receiver Enable — styresignalet der skifter en RS-485-transceiver mellem sende- og modtagetilstand. |
| **Exception (Modbus)** | Modbus-fejlsvar (fx "Illegal Data Address") når en forespørgsel ikke kan opfyldes. |

## IEC 61131-3 / ST Logic-termer

| Term | Forklaring |
|------|------------|
| **ST (Structured Text)** | Et af de fem programmeringssprog defineret i IEC 61131-3-standarden for industriel automation — tekstbaseret, Pascal-lignende syntaks. Se [kapitel 8](08_ST_Logic_Programmering.md). |
| **Scan-cyklus** | Én komplet eksekvering af et PLC-program, fra start til slut, gentaget kontinuerligt. |
| **Funktionsblok (FB)** | Genanvendelig programenhed med intern tilstand mellem kald (fx en timer, der husker hvor langt den er nået). |
| **TON / TOF / TP** | Standard IEC 61131-3-timer-funktionsblokke: Timer-ON-delay, Timer-OFF-delay, Timer-Pulse. |
| **CTU / CTD / CTUD** | Standard tæller-funktionsblokke: Count-Up, Count-Down, Count-Up/Down. |
| **VM (Virtual Machine)** | Systemets interne bytecode-fortolker der eksekverer kompilerede ST-programmer. |
| **Bytecode** | Den kompilerede, maskinnære repræsentation af et ST-program, som VM'en rent faktisk eksekverer. |
| **EXPORT** | ST-nøgleord der gør en variabel synlig som et Modbus Input Register for eksterne systemer. |
| **Binding** | Kobling mellem en ST-variabel og et Modbus-register/coil/discrete input, så værdien synkroniseres automatisk. |
| **R_TRIG / F_TRIG** | Kantdetektions-funktionsblokke: registrerer hhv. stigende og faldende flanke på et boolsk signal. |

## Netværk & system-termer

| Term | Forklaring |
|------|------------|
| **DHCP** | Protokol hvor enheden automatisk får tildelt en IP-adresse fra en netværks-DHCP-server, i stedet for en fast/statisk IP. |
| **NTP** | Network Time Protocol — bruges til at synkronisere enhedens realtidsur mod en tidsserver. |
| **RBAC** | Role-Based Access Control — adgangsstyring baseret på navngivne brugere med tildelte roller og privilegier. Se [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md). |
| **Basic Auth** | HTTP's indbyggede, simple autentificeringsmekanisme (brugernavn:kodeord, base64-kodet i en header). Ikke krypteret i sig selv — kræver HTTPS for reel beskyttelse på ledningen. |
| **OTA (Over-The-Air)** | Firmwareopdatering udført over netværket, uden fysisk USB-tilslutning. Se [kapitel 11](11_Backup_Restore_og_Firmware.md). |
| **NVS (Non-Volatile Storage)** | ESP32'ens indbyggede flash-baserede lagerområde til persisteret konfiguration — overlever strømtab og genstart. |
| **SSE (Server-Sent Events)** | En envejs, vedvarende HTTP-forbindelse hvor serveren løbende sender opdateringer til klienten — bruges til dashboardets real-time visning uden konstant polling. |
| **REST API** | Representational State Transfer — arkitekturstilen for systemets HTTP/JSON-baserede programmeringsgrænseflade. Se [kapitel 7](07_REST_API.md). |
| **CORS** | Cross-Origin Resource Sharing — HTTP-mekanisme der styrer om browser-JavaScript fra én oprindelse (domæne/port) må kalde en anden. |
| **Watchdog** | Hardware-/softwaremekanisme der automatisk genstarter systemet, hvis hovedløkken holder op med at svare inden for en given tidsgrænse. |
| **Rate limiting** | Begrænsning af hvor mange forespørgsler en klient må lave inden for et givet tidsrum, for at forhindre overbelastning. |
| **PSRAM** | Ekstern SPI-tilsluttet RAM-udvidelse på visse ESP32-varianter (fx WROVER-modulet i ES32D26), bruges til at aflaste den interne, mere begrænsede DRAM. |
| **GPIO** | General Purpose Input/Output — en fysisk (eller i dette system: evt. virtuel) pin der kan konfigureres som digital ind- eller udgang. |

---

[← Appendiks B: REST API-reference](B_REST_API_Reference.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks D: ST Logic Funktionsreference →](D_ST_Logic_Funktionsreference.md)
