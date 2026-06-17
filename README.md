# buchhaltung

Backend-Service zur Verwaltung einer Buechersammlung.

## Lokal bauen

Das Repository kann auf einem nicht ausfuehrbaren SMB-Mount liegen. Deshalb
muessen alle Build-Artefakte ausserhalb des Repositorys erzeugt werden. Der
mitgelieferte CMake-Preset nutzt dafuer `~/build/buchhaltung`.

```sh
rm -rf "$HOME/build/buchhaltung"
cmake --preset local
cmake --build --preset local --parallel "$(nproc)"
```

Das erzeugte Programm liegt danach unter:

```sh
"$HOME/build/buchhaltung/buch"
```

## Server starten

```sh
"$HOME/build/buchhaltung/buch" server \
  --host 127.0.0.1 \
  --port 8080 \
  --db "$HOME/.local/share/buchhaltung/buchhaltung.sqlite"
```

Ohne Argumente startet `buch` ebenfalls den Server. Die Datenbank wird beim
Start angelegt und migriert.

Umgebungsvariablen:

```text
BUCH_DB_PATH       Pfad zur SQLite-Datenbank
BUCH_HOST          Listen-Adresse, Standard: 127.0.0.1
BUCH_PORT          Listen-Port, Standard: 8080
GOOGLE_BOOKS_KEY   Optionaler Google-Books-API-Key fuer Lookup-Endpunkte
```

## Docker

Image bauen:

```sh
docker build -t buchhaltung:local .
```

Container starten:

```sh
docker run --rm \
  -p 8080:8080 \
  -v buchhaltung-data:/data \
  -e GOOGLE_BOOKS_KEY="$GOOGLE_BOOKS_KEY" \
  buchhaltung:local
```

bzw.

```sh
docker run --rm \
  -p 8080:8080 \
  -v buchhaltung-data:/data \
  --env-file .env \
  buchhaltung:local
```

Healthcheck:

```sh
curl http://127.0.0.1:8080/healthz
```

## API

Die API spricht JSON.

```text
GET    /healthz
GET    /api/works?text=&author=&series=&reading_status=
POST   /api/works
GET    /api/works/{id}
GET    /api/works/{id}/editions
POST   /api/works/{id}/editions
PUT    /api/works/{id}
DELETE /api/works/{id}

POST   /api/editions
GET    /api/editions/{isbn}
GET    /api/editions/{isbn}/copies
POST   /api/editions/{isbn}/copies
PUT    /api/editions/{isbn}
DELETE /api/editions/{isbn}

POST   /api/copies
GET    /api/copies/{id}
PUT    /api/copies/{id}
DELETE /api/copies/{id}

GET    /api/series
GET    /api/lookup/isbn/{isbn}
```

Der Google-Lookup schreibt nichts in die Datenbank. Clients koennen die
zurueckgegebenen Daten verwenden, muessen Werke, Editionen und Exemplare aber
selbst ueber die direkten `POST`-Endpunkte anlegen.

## Import-Workflow

Der persistente Import-Workflow fuehrt alle UIs durch dieselben Schritte.
Vor dem finalen Commit werden keine Werke, Editionen oder Exemplare angelegt.

```text
POST   /api/import-sessions
GET    /api/import-sessions/{id}
PUT    /api/import-sessions/{id}/isbn
POST   /api/import-sessions/{id}/select-work
PUT    /api/import-sessions/{id}/work
PUT    /api/import-sessions/{id}/series
PUT    /api/import-sessions/{id}/edition
PUT    /api/import-sessions/{id}/copy
POST   /api/import-sessions/{id}/back
POST   /api/import-sessions/{id}/commit
DELETE /api/import-sessions/{id}
```

Session starten:

```json
{
  "isbn": "978-0-306-40615-7"
}
```

Das Backend normalisiert und validiert die ISBN. Es prueft zuerst die lokale
Datenbank und fragt Google Books nur an, wenn die Edition noch nicht existiert.

Moegliche Zustaende:

```text
awaiting_isbn
lookup_failed
select_work
edit_work
edit_series
edit_edition
edit_copy
review
completed
```

Eine Session-Antwort enthaelt immer `state`, `allowed_actions`,
`history_depth` und die aktuellen Drafts. `back` stellt den vorherigen Zustand
mitsamt der dort eingegebenen Daten wieder her.

Werk auswaehlen:

```json
{
  "mode": "existing",
  "work_id": "..."
}
```

oder:

```json
{
  "mode": "new"
}
```

Die `work`-, `series`-, `edition`- und `copy`-Endpunkte speichern jeweils den
kompletten aktuellen Draft und wechseln zum naechsten Zustand. Der
Serien-Draft unterstuetzt `none`, `existing` und `new` sowie Staffel, Position,
Positionslabel und Notizen zur Werk-Serien-Zuordnung. `commit` ist nur im
Zustand `review` erlaubt und schreibt alle notwendigen Datensaetze gemeinsam in
einer SQLite-Transaktion.

Bei bodylosen `POST`-Requests mit `curl` sollte explizit ein leerer Body
gesendet werden:

```sh
curl -X POST -d '' http://127.0.0.1:8080/api/import-sessions/SESSION_ID/back
```

## Einfaches Bash-TUI

Unter `scripts/import-tui.sh` liegt ein einfacher interaktiver Client fuer den
Import-Workflow. Er benoetigt nur Bash, `curl` und `jq`.

Da das Repository auf einem nicht ausfuehrbaren Mount liegen kann, wird das
Script explizit mit Bash gestartet:

```sh
bash scripts/import-tui.sh
```

Standardmaessig verbindet es sich mit:

```text
http://127.0.0.1:8080
```

Eine andere Backend-Adresse kann uebergeben werden:

```sh
BUCH_API_URL=http://192.168.1.20:8080 bash scripts/import-tui.sh
```

Das TUI kann:

```text
- neue Import-Sessions starten
- persistente Sessions anhand ihrer ID fortsetzen
- bestehende oder neue Werke waehlen
- keine, eine bestehende oder eine neue Serie zuordnen
- Werk-, Editions- und Exemplardrafts bearbeiten
- an jedem Workflow-Schritt zurueckspringen
- den Import pruefen und atomar bestaetigen
- Sessions abbrechen und loeschen
```

In den Werk-, Editions- und Exemplarschritten werden alle aktuellen Werte als
nummerierte Felder angezeigt. Ein Feld kann beliebig oft ausgewaehlt und
geaendert werden. Bei der Werteingabe behaelt Enter den bisherigen Wert; `-`
setzt ein optionales Feld auf leer. Erst `w` sendet den vollstaendigen Draft an
das Backend und wechselt zum naechsten Schritt.
