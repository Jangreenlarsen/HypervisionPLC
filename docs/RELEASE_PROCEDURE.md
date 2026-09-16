# Release-procedure (GitHub Releases)

Denne fil beskriver hvordan en bygget firmware bliver publiceret som en
GitHub Release, så brugere kan hente `.bin`-filen og installere den via
`/ota`-sidens manuelle upload (se [kapitel 11](manual/11_Backup_Restore_og_Firmware.md)).

> **Historisk note:** FEAT-169 (fjernet i v7.9.68.9, se BUGS_INDEX.md) lod
> enheden selv hente og installere den nyeste release direkte fra GitHub.
> Den funktion er fjernet (flash-optimering — se BUGS_INDEX.md FEAT-169).
> Releases publiceres stadig via denne procedure, men udelukkende til
> MANUEL download+upload nu.

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

## Asset-navnet `firmware.bin`

Ren navnekonvention nu (ikke længere håndhævet af nogen kode, siden FEAT-169
er fjernet) — hold fast i `firmware.bin` for genkendelighed på tværs af
releases, så brugere ved hvad de skal downloade uden at gætte.

## Versionering af git-tags (bemærk et historisk skift)

Ældre tags i dette repo (`v3.2.0` .. `v4.0.2`) følger en forældet nummerering
fra før `PROJECT_VERSION`-formatet (`X.Y.Z.W`, fire tal) blev indført. Fra og
med denne procedure følger ALLE nye tags nøjagtigt `PROJECT_VERSION` — `git tag
v7.9.10.27` svarer 1:1 til `constants.h`, ingen oversættelse nødvendig.
