# Release-procedure (GitHub Releases)

FEAT-169 tilføjede muligheden for at enheder selv kan hente og installere nye
firmwareversioner direkte fra GitHub Releases (`/system`-siden → "Opdater fra
GitHub"). Denne fil beskriver den anden halvdel: hvordan en bygget firmware
rent faktisk bliver publiceret som en release, så der er noget for enhederne
at finde.

## Hvornår cutter man en release?

**Ikke ved hver eneste version-bump.** Dette projekt bumper `PROJECT_VERSION`
(og tilføjer en `BUGS_INDEX.md`-post) ved *enhver* kodeændring — det ville
give en absurd mængde GitHub Releases at gøre det samme. Cut en release når:

- Der er samlet en stabil, testet mængde ændringer siden sidste release
  (typisk efter en større feature-runde eller en batch af bugfixes).
- Der er en akut/kritisk fix brugere bør have hurtigt.

Det er en manuel, bevidst beslutning hver gang — ikke automatiseret ved hvert
commit.

## Forudsætninger (én gang)

```bash
gh auth login        # kræver 'repo'-scope for at kunne oprette releases
```

Bekræft med `gh auth status`.

## Selve proceduren

1. **Commit og push** alt det arbejde der skal med i denne version — scriptet
   nedenfor committer/pusher **ikke** kildekode selv, kun den nye git-tag.
   ```bash
   git add -A
   git commit -m "..."
   git push
   ```
2. **Kør publiceringsscriptet** fra repo-roden:
   ```bash
   scripts/publish_release.sh
   # eller med en fritekst release-note:
   scripts/publish_release.sh --notes "Kort beskrivelse af denne version"
   ```
   Scriptet:
   - Læser `PROJECT_VERSION` fra `include/constants.h` (fx `7.9.10.27`) og
     danner tag'et `v7.9.10.27`.
   - Afviser at køre hvis der er ukommiterede ændringer, eller hvis tag'et
     allerede findes.
   - Bygger firmwaren (`pio run -e es32d26`).
   - Opretter og pusher git-tag'et.
   - Opretter en GitHub Release med `.pio/build/es32d26/firmware.bin`
     **omdøbt til præcis `firmware.bin`** som vedhæftet asset.
3. **Verificér** at releasen ser rigtig ud på GitHub (`gh release view v<version>`
   eller i browseren), og at asset'et hedder `firmware.bin`.

## Kritisk: asset-navnet skal være `firmware.bin`

`api_handler_ota_github_check()` (`src/ota_handler.cpp`,
`GITHUB_RELEASE_ASSET_NAME`) leder specifikt efter en asset ved navn
`firmware.bin` i den seneste release. Uploades et andet filnavn, finder
enhedernes "Tjek for opdatering"-knap ingen installérbar asset, selvom
releasen findes. Ændres navngivningen i det ene sted, skal den ændres i det
andet også.

## Versionering af git-tags (bemærk et historisk skift)

Ældre tags i dette repo (`v3.2.0` .. `v4.0.2`) følger en forældet nummerering
fra før `PROJECT_VERSION`-formatet (`X.Y.Z.W`, fire tal) blev indført. Fra og
med denne procedure følger ALLE nye tags nøjagtigt `PROJECT_VERSION` — `git tag
v7.9.10.27` svarer 1:1 til `constants.h`, ingen oversættelse nødvendig.

## Hvis GitHub-OTA nogensinde holder op med at virke

`certs/github_ca_bundle.pem` indeholder to pinnede rod-CA'er (se
SECURITY_INDEX.md for den fulde afvejning). Roterer GitHub/Let's
Encrypt/Sectigo en dag netop disse rod-ankre (usandsynligt på kort sigt, men
ikke udelukket), holder GitHub-OTA op med at virke, indtil en ny firmware med
et opdateret bundle er installeret — **manuel `.bin`-upload er altid en
fungerende fallback-vej** uanset dette, og kræver ingen af GitHub-deletene
ovenfor.
