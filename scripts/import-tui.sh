#!/usr/bin/env bash

set -u

BASE_URL="${BUCH_API_URL:-http://127.0.0.1:8080}"
SESSION_ID=""
SESSION_JSON="{}"

if ! command -v curl >/dev/null 2>&1; then
    printf 'Fehler: curl ist nicht installiert.\n' >&2
    exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
    printf 'Fehler: jq ist nicht installiert.\n' >&2
    exit 1
fi

clear_screen() {
    printf '\033[2J\033[H'
}

pause() {
    printf '\nWeiter mit Enter...'
    read -r _
}

header() {
    clear_screen
    printf 'Buchhaltung - Import\n'
    printf 'API: %s\n' "$BASE_URL"
    if [[ -n "$SESSION_ID" ]]; then
        printf 'Session: %s\n' "$SESSION_ID"
    fi
    printf '%s\n\n' '----------------------------------------'
}

request() {
    local method="$1"
    local path="$2"
    local body="${3-}"
    local response_file
    local status

    response_file=$(mktemp)

    if [[ "$method" == "GET" ]]; then
        status=$(curl -sS -o "$response_file" -w '%{http_code}' \
            "$BASE_URL$path") || {
            rm -f "$response_file"
            printf 'API nicht erreichbar: %s\n' "$BASE_URL" >&2
            return 1
        }
    elif [[ "$method" == "DELETE" ]]; then
        status=$(curl -sS -o "$response_file" -w '%{http_code}' \
            -X DELETE "$BASE_URL$path") || {
            rm -f "$response_file"
            return 1
        }
    elif [[ -n "$body" ]]; then
        status=$(curl -sS -o "$response_file" -w '%{http_code}' \
            -X "$method" \
            -H 'Content-Type: application/json' \
            --data-binary "$body" \
            "$BASE_URL$path") || {
            rm -f "$response_file"
            return 1
        }
    else
        status=$(curl -sS -o "$response_file" -w '%{http_code}' \
            -X "$method" \
            -d '' \
            "$BASE_URL$path") || {
            rm -f "$response_file"
            return 1
        }
    fi

    if (( status < 200 || status >= 300 )); then
        printf 'API-Fehler (HTTP %s): ' "$status" >&2
        jq -r '.error // .' "$response_file" 2>/dev/null >&2 || cat "$response_file" >&2
        rm -f "$response_file"
        return 1
    fi

    cat "$response_file"
    rm -f "$response_file"
}

refresh_session() {
    SESSION_JSON=$(request GET "/api/import-sessions/$SESSION_ID") || return 1
}

submit_session_request() {
    local method="$1"
    local path="$2"
    local body="${3-}"
    local response

    response=$(request "$method" "$path" "$body") || {
        pause
        return 1
    }

    SESSION_JSON="$response"
    SESSION_ID=$(jq -r '.id' <<<"$SESSION_JSON")
}

json_string() {
    local json="$1"
    local path="$2"
    jq -r "$path // empty" <<<"$json"
}

json_int() {
    local json="$1"
    local path="$2"
    jq -r "$path // empty" <<<"$json"
}

prompt_string() {
    local label="$1"
    local current="$2"
    local answer

    if [[ -n "$current" ]]; then
        printf '%s [%s]: ' "$label" "$current" >&2
    else
        printf '%s: ' "$label" >&2
    fi
    read -r answer

    if [[ -z "$answer" ]]; then
        printf '%s' "$current"
    elif [[ "$answer" == "-" ]]; then
        printf ''
    else
        printf '%s' "$answer"
    fi
}

prompt_required() {
    local label="$1"
    local current="$2"
    local answer

    while true; do
        answer=$(prompt_string "$label" "$current")
        if [[ -n "$answer" ]]; then
            printf '%s' "$answer"
            return
        fi
        printf 'Dieses Feld darf nicht leer sein.\n' >&2
    done
}

prompt_integer() {
    local label="$1"
    local current="$2"
    local answer

    while true; do
        answer=$(prompt_string "$label" "$current")
        if [[ -z "$answer" || "$answer" =~ ^-?[0-9]+$ ]]; then
            printf '%s' "$answer"
            return
        fi
        printf 'Bitte eine Ganzzahl eingeben.\n' >&2
    done
}

prompt_number() {
    local label="$1"
    local current="$2"
    local answer

    while true; do
        answer=$(prompt_string "$label" "$current")
        if [[ -z "$answer" || "$answer" =~ ^-?[0-9]+([.][0-9]+)?$ ]]; then
            printf '%s' "$answer"
            return
        fi
        printf 'Bitte eine Zahl mit optionalem Dezimalpunkt eingeben.\n' >&2
    done
}

edit_json_string() {
    local json="$1"
    local path="$2"
    local label="$3"
    local required="${4-false}"
    local current
    local value

    current=$(jq -r "$path // empty" <<<"$json")
    if [[ "$required" == "true" ]]; then
        value=$(prompt_required "$label" "$current")
        jq --arg value "$value" "$path = \$value" <<<"$json"
    else
        value=$(prompt_string "$label" "$current")
        if [[ -z "$value" ]]; then
            jq "$path = null" <<<"$json"
        else
            jq --arg value "$value" "$path = \$value" <<<"$json"
        fi
    fi
}

edit_json_integer() {
    local json="$1"
    local path="$2"
    local label="$3"
    local current
    local value

    current=$(jq -r "$path // empty" <<<"$json")
    value=$(prompt_integer "$label" "$current")
    if [[ -z "$value" ]]; then
        jq "$path = null" <<<"$json"
    else
        jq --argjson value "$value" "$path = \$value" <<<"$json"
    fi
}

edit_json_number() {
    local json="$1"
    local path="$2"
    local label="$3"
    local current
    local value

    current=$(jq -r "$path // empty" <<<"$json")
    value=$(prompt_number "$label" "$current")
    if [[ -z "$value" ]]; then
        jq "$path = null" <<<"$json"
    else
        jq --argjson value "$value" "$path = \$value" <<<"$json"
    fi
}

edit_json_string_array() {
    local json="$1"
    local path="$2"
    local label="$3"
    local current
    local value
    local array

    current=$(jq -r "$path // [] | join(\", \")" <<<"$json")
    value=$(prompt_string "$label" "$current")
    array=$(csv_to_json "$value")
    jq --argjson value "$array" "$path = \$value" <<<"$json"
}

csv_to_json() {
    local value="$1"
    jq -Rn --arg value "$value" \
        '$value | split(",") | map(gsub("^\\s+|\\s+$"; "")) | map(select(length > 0))'
}

show_lookup() {
    local lookup
    lookup=$(jq '.lookup' <<<"$SESSION_JSON")

    printf 'Google Books\n\n'
    printf 'Titel:       %s\n' "$(json_string "$lookup" '.title')"
    printf 'Untertitel:  %s\n' "$(json_string "$lookup" '.subtitle')"
    printf 'Autoren:     %s\n' "$(jq -r '.authors // [] | join(", ")' <<<"$lookup")"
    printf 'Verlag:      %s\n' "$(json_string "$lookup" '.publisher')"
    printf 'Datum:       %s\n' "$(json_string "$lookup" '.published_date')"
    printf 'Sprache:     %s\n' "$(json_string "$lookup" '.language')"
    printf 'ISBN-10:     %s\n' "$(json_string "$lookup" '.isbn_10')"
    printf 'ISBN-13:     %s\n' "$(json_string "$lookup" '.isbn_13')"
    printf 'Seiten:      %s\n' "$(json_int "$lookup" '.page_count')"
}

start_session() {
    local isbn
    local body
    local response

    header
    printf 'ISBN eingeben\n\n'
    printf 'ISBN: '
    read -r isbn

    if [[ -z "$isbn" ]]; then
        return 1
    fi

    body=$(jq -n --arg isbn "$isbn" '{isbn: $isbn}')
    response=$(request POST '/api/import-sessions' "$body") || {
        pause
        return 1
    }

    SESSION_JSON="$response"
    SESSION_ID=$(jq -r '.id' <<<"$SESSION_JSON")
}

handle_awaiting_isbn() {
    local isbn
    local body

    header
    printf 'ISBN eingeben\n\n'
    printf 'ISBN, "b" fuer Zurueck oder "q" zum Abbrechen: '
    read -r isbn
    [[ -n "$isbn" ]] || return

    if [[ "$isbn" == "b" || "$isbn" == "B" ]]; then
        submit_session_request POST "/api/import-sessions/$SESSION_ID/back"
        return
    fi
    if [[ "$isbn" == "q" || "$isbn" == "Q" ]]; then
        cancel_session
        return
    fi

    body=$(jq -n --arg isbn "$isbn" '{isbn: $isbn}')
    submit_session_request PUT "/api/import-sessions/$SESSION_ID/isbn" "$body"
}

handle_lookup_failed() {
    local choice

    header
    printf 'Lookup fehlgeschlagen\n\n'
    jq -r '.lookup_error // "Unbekannter Fehler"' <<<"$SESSION_JSON"
    printf '\n1) Andere ISBN eingeben\n'
    printf '2) Zurueck\n'
    printf '3) Abbrechen\n\n'
    printf 'Auswahl: '
    read -r choice

    case "$choice" in
        1) handle_awaiting_isbn ;;
        2) submit_session_request POST "/api/import-sessions/$SESSION_ID/back" ;;
        3) cancel_session ;;
    esac
}

handle_select_work() {
    local count
    local index
    local choice
    local work_id
    local body

    header
    show_lookup
    printf '\nMoegliche bestehende Werke\n\n'

    count=$(jq '.work_candidates | length' <<<"$SESSION_JSON")
    if (( count == 0 )); then
        printf 'Keine Kandidaten gefunden.\n'
    else
        for ((index = 0; index < count; ++index)); do
            printf '%d) %s (%s)\n' \
                "$((index + 1))" \
                "$(jq -r ".work_candidates[$index].title" <<<"$SESSION_JSON")" \
                "$(jq -r ".work_candidates[$index].authors | join(\", \")" <<<"$SESSION_JSON")"
        done
    fi

    printf '\nn) Neues Werk anlegen\n'
    printf 'b) Zurueck\n'
    printf 'q) Abbrechen\n\n'
    printf 'Auswahl: '
    read -r choice

    case "$choice" in
        n|N)
            body='{"mode":"new"}'
            submit_session_request POST "/api/import-sessions/$SESSION_ID/select-work" "$body"
            ;;
        b|B)
            submit_session_request POST "/api/import-sessions/$SESSION_ID/back"
            ;;
        q|Q)
            cancel_session
            ;;
        *)
            if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= count )); then
                work_id=$(jq -r ".work_candidates[$((choice - 1))].id" <<<"$SESSION_JSON")
                body=$(jq -n --arg work_id "$work_id" '{mode: "existing", work_id: $work_id}')
                submit_session_request POST "/api/import-sessions/$SESSION_ID/select-work" "$body"
            fi
            ;;
    esac
}

handle_edit_work() {
    local draft
    local choice

    draft=$(jq '.work' <<<"$SESSION_JSON")

    while true; do
        header
        printf 'Werk bearbeiten\n'
        printf 'Feld waehlen. Bei der Eingabe behaelt Enter den Wert, "-" leert ihn.\n\n'
        printf '1) Titel:             %s\n' "$(json_string "$draft" '.canonical_title')"
        printf '2) Originaltitel:     %s\n' "$(json_string "$draft" '.original_title')"
        printf '3) Originalsprache:   %s\n' "$(json_string "$draft" '.original_language_code')"
        printf '4) Erstes Jahr:       %s\n' "$(json_int "$draft" '.first_published_year')"
        printf '5) Autoren:           %s\n' "$(jq -r '.authors // [] | join(", ")' <<<"$draft")"
        printf '6) Beschreibung:      %s\n' "$(json_string "$draft" '.description')"
        printf '7) Altersfreigabe:    %s\n' "$(json_string "$draft" '.age_rating')"
        printf '8) Notizen:           %s\n' "$(json_string "$draft" '.notes')"
        printf '\nw) Weiter  b) Zurueck  q) Abbrechen\n\n'
        printf 'Auswahl: '
        read -r choice

        case "$choice" in
            1) draft=$(edit_json_string "$draft" '.canonical_title' 'Titel' true) ;;
            2) draft=$(edit_json_string "$draft" '.original_title' 'Originaltitel') ;;
            3) draft=$(edit_json_string "$draft" '.original_language_code' 'Originalsprache') ;;
            4) draft=$(edit_json_integer "$draft" '.first_published_year' 'Erstveroeffentlichungsjahr') ;;
            5) draft=$(edit_json_string_array "$draft" '.authors' 'Autoren, kommasepariert') ;;
            6) draft=$(edit_json_string "$draft" '.description' 'Beschreibung') ;;
            7) draft=$(edit_json_string "$draft" '.age_rating' 'Altersfreigabe') ;;
            8) draft=$(edit_json_string "$draft" '.notes' 'Notizen') ;;
            w|W)
                submit_session_request PUT "/api/import-sessions/$SESSION_ID/work" "$draft"
                return
                ;;
            b|B)
                submit_session_request POST "/api/import-sessions/$SESSION_ID/back"
                return
                ;;
            q|Q)
                cancel_session
                return
                ;;
        esac
    done
}

handle_edit_series() {
    local draft
    local choice
    local mode
    local count
    local index
    local selected_name
    local mode_label

    draft=$(jq '.series' <<<"$SESSION_JSON")

    while true; do
        mode=$(jq -r '.mode // "none"' <<<"$draft")
        selected_name=""
        mode_label="keine Serie"
        if [[ "$mode" == "existing" ]]; then
            mode_label="vorhandene Serie"
            selected_name=$(jq -r \
                --arg id "$(json_string "$draft" '.existing_series_id')" \
                '.series_options[]? | select(.id == $id) | .name' \
                <<<"$SESSION_JSON")
        elif [[ "$mode" == "new" ]]; then
            mode_label="neue Serie"
            selected_name=$(json_string "$draft" '.name')
        fi

        header
        printf 'Serie zuordnen\n'
        printf 'Standard ist keine Serie. Mit "w" geht es direkt ohne Zuordnung weiter.\n\n'
        printf ' 1) Modus:                 %s\n' "$mode_label"
        printf ' 2) Serie:                 %s\n' "$selected_name"
        printf ' 3) Originaltitel:         %s\n' "$(json_string "$draft" '.original_title')"
        printf ' 4) Serienbeschreibung:    %s\n' "$(json_string "$draft" '.description')"
        printf ' 5) Seriennotizen:         %s\n' "$(json_string "$draft" '.notes')"
        printf ' 6) Staffel:               %s\n' "$(json_int "$draft" '.season')"
        printf ' 7) Position/Band:         %s\n' "$(jq -r '.position // empty' <<<"$draft")"
        printf ' 8) Positionslabel:        %s\n' "$(json_string "$draft" '.position_label')"
        printf ' 9) Zuordnungsnotizen:     %s\n' "$(json_string "$draft" '.work_series_notes')"
        printf '\nw) Weiter  b) Zurueck  q) Abbrechen\n\n'
        printf 'Auswahl: '
        read -r choice

        case "$choice" in
            1)
                printf '\nn) Keine Serie\n'
                printf 'v) Vorhandene Serie\n'
                printf 'a) Neue Serie anlegen\n'
                printf 'Auswahl: '
                read -r choice
                case "$choice" in
                    n|N)
                        draft=$(jq '.mode = "none" | .existing_series_id = null | .name = null' <<<"$draft")
                        ;;
                    v|V)
                        draft=$(jq '.mode = "existing" | .name = null' <<<"$draft")
                        ;;
                    a|A)
                        draft=$(jq '.mode = "new" | .existing_series_id = null' <<<"$draft")
                        ;;
                esac
                ;;
            2)
                mode=$(jq -r '.mode // "none"' <<<"$draft")
                if [[ "$mode" == "existing" ]]; then
                    count=$(jq '.series_options | length' <<<"$SESSION_JSON")
                    if (( count == 0 )); then
                        printf 'Es sind noch keine Serien vorhanden.\n'
                        pause
                    else
                        printf '\n'
                        for ((index = 0; index < count; ++index)); do
                            printf '%d) %s\n' \
                                "$((index + 1))" \
                                "$(jq -r ".series_options[$index].name" <<<"$SESSION_JSON")"
                        done
                        printf 'Auswahl: '
                        read -r choice
                        if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= count )); then
                            draft=$(jq \
                                --arg id "$(jq -r ".series_options[$((choice - 1))].id" <<<"$SESSION_JSON")" \
                                '.existing_series_id = $id' \
                                <<<"$draft")
                        fi
                    fi
                elif [[ "$mode" == "new" ]]; then
                    draft=$(edit_json_string "$draft" '.name' 'Serienname' true)
                else
                    printf 'Bitte zuerst den Modus "vorhanden" oder "neu" waehlen.\n'
                    pause
                fi
                ;;
            3)
                if [[ "$mode" == "new" ]]; then
                    draft=$(edit_json_string "$draft" '.original_title' 'Originaltitel')
                fi
                ;;
            4)
                if [[ "$mode" == "new" ]]; then
                    draft=$(edit_json_string "$draft" '.description' 'Serienbeschreibung')
                fi
                ;;
            5)
                if [[ "$mode" == "new" ]]; then
                    draft=$(edit_json_string "$draft" '.notes' 'Seriennotizen')
                fi
                ;;
            6) draft=$(edit_json_integer "$draft" '.season' 'Staffel') ;;
            7) draft=$(edit_json_number "$draft" '.position' 'Position/Band') ;;
            8) draft=$(edit_json_string "$draft" '.position_label' 'Positionslabel') ;;
            9) draft=$(edit_json_string "$draft" '.work_series_notes' 'Zuordnungsnotizen') ;;
            w|W)
                submit_session_request PUT "/api/import-sessions/$SESSION_ID/series" "$draft"
                return
                ;;
            b|B)
                submit_session_request POST "/api/import-sessions/$SESSION_ID/back"
                return
                ;;
            q|Q)
                cancel_session
                return
                ;;
        esac
    done
}

handle_edit_edition() {
    local draft
    local choice
    local format_value

    draft=$(jq '.edition' <<<"$SESSION_JSON")

    while true; do
        header
        printf 'Edition bearbeiten\n'
        printf 'Feld waehlen. Bei der Eingabe behaelt Enter den Wert, "-" leert ihn.\n\n'
        printf ' 1) ISBN:              %s\n' "$(json_string "$draft" '.isbn')"
        printf ' 2) Titel:             %s\n' "$(json_string "$draft" '.title')"
        printf ' 3) Untertitel:        %s\n' "$(json_string "$draft" '.subtitle')"
        printf ' 4) Sprache:           %s\n' "$(json_string "$draft" '.language_code')"
        printf ' 5) Verlag:            %s\n' "$(json_string "$draft" '.publisher')"
        printf ' 6) Erscheinungsjahr:  %s\n' "$(json_int "$draft" '.publication_year')"
        printf ' 7) Ausgabename:       %s\n' "$(json_string "$draft" '.edition_name')"
        printf ' 8) Format:            %s\n' "$(json_string "$draft" '.format')"
        printf ' 9) Seiten:            %s\n' "$(json_int "$draft" '.page_count')"
        printf '10) Cover-URL:         %s\n' "$(json_string "$draft" '.cover_url')"
        printf '11) Notizen:           %s\n' "$(json_string "$draft" '.notes')"
        printf '\nw) Weiter  b) Zurueck  q) Abbrechen\n\n'
        printf 'Auswahl: '
        read -r choice

        case "$choice" in
            1) draft=$(edit_json_string "$draft" '.isbn' 'ISBN' true) ;;
            2) draft=$(edit_json_string "$draft" '.title' 'Titel' true) ;;
            3) draft=$(edit_json_string "$draft" '.subtitle' 'Untertitel') ;;
            4) draft=$(edit_json_string "$draft" '.language_code' 'Sprache' true) ;;
            5) draft=$(edit_json_string "$draft" '.publisher' 'Verlag') ;;
            6) draft=$(edit_json_integer "$draft" '.publication_year' 'Erscheinungsjahr') ;;
            7) draft=$(edit_json_string "$draft" '.edition_name' 'Ausgabename') ;;
            8)
                printf '\nt) Taschenbuch\n'
                printf 'h) Hardcover\n'
                printf 'a) Anderes Format eingeben\n'
                printf 'l) Format leeren\n'
                printf 'Auswahl: '
                read -r choice
                case "$choice" in
                    t|T) draft=$(jq '.format = "Taschenbuch"' <<<"$draft") ;;
                    h|H) draft=$(jq '.format = "Hardcover"' <<<"$draft") ;;
                    a|A)
                        format_value=$(prompt_string 'Format' "$(json_string "$draft" '.format')")
                        if [[ -z "$format_value" ]]; then
                            draft=$(jq '.format = null' <<<"$draft")
                        else
                            draft=$(jq --arg value "$format_value" '.format = $value' <<<"$draft")
                        fi
                        ;;
                    l|L) draft=$(jq '.format = null' <<<"$draft") ;;
                esac
                ;;
            9) draft=$(edit_json_integer "$draft" '.page_count' 'Seiten') ;;
            10) draft=$(edit_json_string "$draft" '.cover_url' 'Cover-URL') ;;
            11) draft=$(edit_json_string "$draft" '.notes' 'Notizen') ;;
            w|W)
                submit_session_request PUT "/api/import-sessions/$SESSION_ID/edition" "$draft"
                return
                ;;
            b|B)
                submit_session_request POST "/api/import-sessions/$SESSION_ID/back"
                return
                ;;
            q|Q)
                cancel_session
                return
                ;;
        esac
    done
}

handle_edit_copy() {
    local draft
    local choice

    draft=$(jq '.copy' <<<"$SESSION_JSON")

    while true; do
        header
        printf 'Exemplar bearbeiten\n'
        if jq -e '.existing_edition == true' >/dev/null <<<"$SESSION_JSON"; then
            printf 'Die Edition existiert bereits. Es wird nur ein Exemplar angelegt.\n'
        fi
        printf 'Feld waehlen. Bei der Eingabe behaelt Enter den Wert, "-" leert ihn.\n\n'
        printf '1) Standortpfad:       %s\n' "$(json_string "$draft" '.location_path')"
        printf '2) Position:           %s\n' "$(json_int "$draft" '.position_in_location')"
        printf '3) Zustand:            %s\n' "$(json_string "$draft" '.condition')"
        printf '4) Erworben am:        %s\n' "$(json_string "$draft" '.acquired_date')"
        printf '5) Erworben bei/von:   %s\n' "$(json_string "$draft" '.acquired_where')"
        printf '6) Geliehen von:       %s\n' "$(json_string "$draft" '.borrowed_from')"
        printf '7) Verliehen an:       %s\n' "$(json_string "$draft" '.lent_to')"
        printf '8) Notizen:            %s\n' "$(json_string "$draft" '.notes')"
        printf '\nw) Weiter  b) Zurueck  q) Abbrechen\n\n'
        printf 'Auswahl: '
        read -r choice

        case "$choice" in
            1) draft=$(edit_json_string "$draft" '.location_path' 'Standortpfad') ;;
            2) draft=$(edit_json_integer "$draft" '.position_in_location' 'Position') ;;
            3) draft=$(edit_json_string "$draft" '.condition' 'Zustand') ;;
            4) draft=$(edit_json_string "$draft" '.acquired_date' 'Erworben am') ;;
            5) draft=$(edit_json_string "$draft" '.acquired_where' 'Erworben bei/von') ;;
            6) draft=$(edit_json_string "$draft" '.borrowed_from' 'Geliehen von') ;;
            7) draft=$(edit_json_string "$draft" '.lent_to' 'Verliehen an') ;;
            8) draft=$(edit_json_string "$draft" '.notes' 'Notizen') ;;
            w|W)
                submit_session_request PUT "/api/import-sessions/$SESSION_ID/copy" "$draft"
                return
                ;;
            b|B)
                submit_session_request POST "/api/import-sessions/$SESSION_ID/back"
                return
                ;;
            q|Q)
                cancel_session
                return
                ;;
        esac
    done
}

handle_review() {
    local choice

    header
    printf 'Import pruefen\n\n'
    printf 'Werk:\n'
    jq '.work' <<<"$SESSION_JSON"
    printf '\nSerie:\n'
    jq '.series' <<<"$SESSION_JSON"
    printf '\nEdition:\n'
    jq '.edition' <<<"$SESSION_JSON"
    printf '\nExemplar:\n'
    jq '.copy' <<<"$SESSION_JSON"

    printf '\n1) Import bestaetigen\n'
    printf '2) Zurueck\n'
    printf '3) Abbrechen\n\n'
    printf 'Auswahl: '
    read -r choice

    case "$choice" in
        1) submit_session_request POST "/api/import-sessions/$SESSION_ID/commit" ;;
        2) submit_session_request POST "/api/import-sessions/$SESSION_ID/back" ;;
        3) cancel_session ;;
    esac
}

handle_completed() {
    header
    printf 'Import abgeschlossen\n\n'
    jq '.result' <<<"$SESSION_JSON"
    printf '\n'
    pause
    SESSION_ID=""
    SESSION_JSON="{}"
}

cancel_session() {
    if [[ -n "$SESSION_ID" ]]; then
        request DELETE "/api/import-sessions/$SESSION_ID" >/dev/null || true
    fi
    SESSION_ID=""
    SESSION_JSON="{}"
}

main_menu() {
    local choice

    header
    printf '1) Import starten\n'
    printf '2) Vorhandene Session fortsetzen\n'
    printf '3) Beenden\n\n'
    printf 'Auswahl: '
    read -r choice

    case "$choice" in
        1) start_session ;;
        2)
            printf 'Session-ID: '
            read -r SESSION_ID
            if [[ -n "$SESSION_ID" ]] && ! refresh_session; then
                SESSION_ID=""
                pause
            fi
            ;;
        3) exit 0 ;;
    esac
}

while true; do
    if [[ -z "$SESSION_ID" ]]; then
        main_menu
        continue
    fi

    state=$(jq -r '.state' <<<"$SESSION_JSON")
    case "$state" in
        awaiting_isbn) handle_awaiting_isbn ;;
        lookup_failed) handle_lookup_failed ;;
        select_work) handle_select_work ;;
        edit_work) handle_edit_work ;;
        edit_series) handle_edit_series ;;
        edit_edition) handle_edit_edition ;;
        edit_copy) handle_edit_copy ;;
        review) handle_review ;;
        completed) handle_completed ;;
        *)
            header
            printf 'Unbekannter Session-Zustand: %s\n' "$state"
            pause
            cancel_session
            ;;
    esac
done
