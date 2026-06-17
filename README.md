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
