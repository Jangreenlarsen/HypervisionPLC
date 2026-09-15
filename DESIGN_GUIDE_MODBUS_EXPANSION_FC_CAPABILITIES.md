# Design Guide: Function Code Capability Reporting (Modbus Expansion Board)

**Status:** Designforslag — ikke implementeret på boardet. Til brug for expansion-board-udviklingsteamet (søster-repo "HypervisionPLC Extension Board").
**Målgruppe:** En udvikler (menneske eller Claude-session) der arbejder i expansion-board-firmwarens repo.
**Ophav:** Hypervision PLC-projektet, efter at PLC-siden (v7.9.68.0) fik tre nye ST Logic-funktioner (`MB_WRITE_COILS`, `MBX_WRITE_HOLDINGS`, `MBX_WRITE_COILS` — FC15/FC16) og et empirisk "Funktions-test"-panel (v7.9.68.2, System-siden → Modbus Expansion Boards) til at afprøve hvilke function codes et tilsluttet board rent faktisk understøtter. Dette dokument beskriver den **rigtige** løsning på det problem, det empiriske testpanel kun kan tilnærme.
**Afhængigheder:** Ingen kodeadgang til PLC-repoet nødvendig — kun `PLC_INTEGRATION_MANUAL.md` (allerede i jeres besiddelse, det ER jeres egen kontrakt) og dette dokument.

---

## 0. Problemet

`PLC_INTEGRATION_MANUAL.md` §3.2 dokumenterer i dag en **statisk, hardkodet liste**: *"Understøttede function codes: FC01, FC02, FC03, FC04, FC05, FC06, FC16. Andet giver en gateway-exception."* Der findes ingen API til at forespørge dette **live**, fra et konkret, kørende board.

Det er et problem af tre grunde:

1. **Firmware-versioner driver i praksis fra hinanden.** Et board i felten kører muligvis en ældre firmware end det seneste dokumenterede FC-sæt — eller (efter I implementerer denne guide) et nyere med FLERE FC'er end den PLC-udgave, der taler med det, ved af. Uden en runtime-forespørgsel må PLC'en enten anteage det værste, eller stole blindt på en statisk tekst der kan være forældet.
2. **PLC-siden kan i dag kun teste empirisk** — sende en rigtig Modbus-transaktion og fortolke svaret. For **skrive**-FC'er (05/06/15/16) betyder det et RIGTIGT skriv til udstyret bag boardet, med de sideeffekter det har (se PLC-sidens eget "Funktions-test"-panel, som eksplicit advarer brugeren om dette og kræver bekræftelse).
3. **Selv den empiriske test er kun delvist pålidelig.** REST-diagnose-endpointet (§4.5/§4.6) returnerer i dag `error_code:7` (`MB_INVALID_ADDRESS`, beskrevet som *"ugyldig/ukendt function code eller PDU-længde i selve requestet"*) når en FC afvises — PLC-siden bruger denne til at gætte "boardet understøtter ikke denne FC", men det er en **fortolkning af en fejlkode designet til noget bredere** (den dækker også "ugyldig adresse i en ellers gyldig FC"), ikke en eksplicit, dedikeret "unsupported function code"-status. Det er skrøbeligt at bygge videre på.

---

## 1. Anbefalet løsning: `GET /api/capabilities`

Et nyt, uautentificeret-eller-autentificeret (samme regel som `/api/status`, §4.1) endpoint, der **deklarerer** support statisk — ingen bus-trafik, ingen sideeffekter, svarer øjeblikkeligt uanset om nogen slave er tilsluttet eller online.

```json
GET /api/capabilities

{
  "api_version": 1,
  "fw_version": "0.13.0",
  "modbus_tcp": {
    "supported_function_codes": [1, 2, 3, 4, 5, 6, 16],
    "max_read_quantity": 2000,
    "max_write_quantity": 32
  },
  "rest_diagnostic": {
    "supported_function_codes": [1, 2, 3, 4, 5, 6, 16]
  }
}
```

**Designvalg og begrundelse:**

- **To separate lister** (`modbus_tcp` vs. `rest_diagnostic`) i stedet for én fælles — de KAN divergere. Vi oplevede det allerede på PLC-siden: da FC16 blev tilføjet til den kontinuerlige Modbus TCP-datavej, var REST-diagnosepanelets egen dispatcher ikke opdateret til FC15 samtidig (en ren PLC-side-bug, siden rettet) — samme klasse af "de to lag drifter fra hinanden"-fejl kan opstå hos jer, og en fælles liste ville skjule det.
- **`fw_version` gentaget her** (findes allerede i `/api/status`) — så PLC-siden kan cache et boards capabilities sammen med den firmware-version de blev observeret under, uden at skulle korrelere to separate kald.
- **`max_read_quantity`/`max_write_quantity`** inkluderet, fordi PLC-manualens §4.5 allerede nævner *"Modbus-spec'ens egne grænser pr. function code håndhæves af kanal-laget"* uden at sige hvad de faktisk ER — en oplagt lejlighed til at gøre en implicit grænse eksplicit og forespørgelig, samme motivation som resten af dette dokument.
- **`api_version:1`** — samme mønster som `/api/status` allerede bruger; bump KUN ved brydende ændringer af selve `/api/capabilities`-kontrakten.

### 1.1 Alternativ med lavere implementeringsomkostning

Hvis et helt nyt endpoint er mere end I ønsker at committe til lige nu: udvid i stedet det **eksisterende** `GET /api/status` (§4.2) med et `supported_function_codes`-felt:

```json
{
  "api_version": 1,
  "fw_version": "0.13.0",
  "fw_build": "0015",
  "uptime_s": 86412,
  "supported_function_codes": [1, 2, 3, 4, 5, 6, 16],
  ...
}
```

Enklere at implementere (ingen ny route, ingen ny auth-vurdering — `/api/status` er allerede polled periodisk af PLC'en, §6 punkt 2), men mister muligheden for at skelne Modbus TCP- fra REST-diagnose-support, og roder et i forvejen letvægts "er boardet oppe"-endpoint sammen med et statisk capability-svar. **Vores anbefaling er stadig det separate endpoint (§1)**, men denne variant er et legitimt, hurtigere første skridt.

---

## 2. Supplerende, uafhængig forbedring: en dedikeret "unsupported function code"-status

Selv med `/api/capabilities` på plads vil PLC'en fortsat ramme situationer hvor et LIVE forsøg på at bruge en FC fejler — og det er stadig værdifuldt at kunne skelne *"boardet forstod ikke denne FC"* fra *"slaven svarede ikke"*/*"kanalen var optaget"* i selve fejlsvaret, uafhængigt af om PLC'en har spurgt `/api/capabilities` på forhånd (fx efter en firmware-opdatering af boardet, før PLC'en har cachet nye capabilities).

**Forslag:** tilføj en ny, dedikeret `mb_error_code_t`-værdi — fx `10` — specifikt for *"function code ikke implementeret af boardets gateway"*, adskilt fra det nuværende, bredere `7` (`MB_INVALID_ADDRESS`, som fortsat dækker "ugyldig adresse i en ellers gyldig FC"). Brug den:

- I REST-diagnose-svaret (§4.5/§4.6): `{"ok":false,"error_code":10,"error":"channel_error","message":"Function code X ikke understøttet af dette board"}`.
- På selve Modbus TCP-data-planet (§3.2/§3.3): fortsat en standard Modbus-gateway-exception (høj bit sat) — men overvej at bruge den Modbus-**standardiserede** `0x01` (Illegal Function) specifikt for denne situation, i stedet for det nuværende `0x0A` (Gateway Path Unavailable, som §3.2 i dag bruger til BÅDE "ugyldig FC", "deaktiveret kanal" OG "kanal optaget" — tre reelt forskellige situationer under én kode). `0x01` er den kode en almindelig Modbus-master forventer at se for netop "denne function code kender jeg ikke" — at bruge den her ville gøre boardets gateway-exception-adfærd tættere på hvad en standard Modbus TCP-klient (ikke kun denne PLC) allerede ved hvordan den skal fortolke.

Dette er valgfrit og kan committes uafhængigt af §1 — men de to supplerer hinanden godt: `/api/capabilities` for planlægning/UI, en klar fejlkode for det sjældne tilfælde hvor et live-kald alligevel rammer en FC der ikke (længere) understøttes.

---

## 3. Hvordan PLC-siden vil bruge det

1. **Ved boardets provisionering/første tilføjelse** (`show modbus-expansion`/System-siden): kald `/api/capabilities` én gang, cache resultatet sammen med boardets `id`.
2. **Ved hver periodiske `/api/status`-healthcheck** (§6 punkt 2, i dag hvert 30.-60. sekund): sammenlign `fw_version` mod den cachede capabilities-snapshots `fw_version` — ved mismatch (boardet er blevet opdateret siden sidst), genforespørg `/api/capabilities`.
3. **I System-sidens "Modbus Expansion Boards"-kort:** vis de deklarerede FC'er direkte (fx en badge-række "FC01 FC02 FC03 FC04 FC05 FC06 FC16" per board) — ingen live-trafik, ingen bekræftelses-dialog nødvendig, opdateres på hvert kanal-skift.
4. **Det eksisterende empiriske "Funktions-test"-panel bevares som supplement**, ikke erstatning — nyttigt til reelt at BEKRÆFTE at en deklareret FC også virker end-to-end mod den faktiske fysiske slave bag boardet (deklareret support ≠ garanti for at den konkrete RTU-slave på den konkrete adresse også taler den FC).

---

## 4. Ikke i scope for denne guide

- Runtime-ændring af hvilke FC'er der er aktive (dvs. ingen `PUT /api/capabilities`) — function code-support er en egenskab ved firmwaren, ikke en køretids-indstilling.
- Per-kanal FC-support (fx hvis kanal A og B nogensinde skulle understøtte forskellige FC-sæt) — antaget globalt éns for hele boardet i dette forslag; udvid til en per-kanal-liste hvis det bliver relevant.
