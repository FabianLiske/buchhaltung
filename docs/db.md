```text
works
- id: UUID, primary key
- canonical_title: text
- original_title: text, optional
- original_language_code: text, optional
- first_published_year: integer, optional
- description: text, optional
- age_rating: text, optional
- notes: text, optional
```

```text
editions
- isbn: text, primary key
- work_id: UUID, references works.id
- title: text
- subtitle: text, optional
- language_code: text
- publisher: text, optional
- publication_year: integer, optional
- edition_name: text, optional
- format: text, optional
- page_count: integer, optional
- cover_url: text, optional
- notes: text, optional
```

```text
copies
- id: UUID, primary key
- edition_isbn: text, references editions.isbn
- location_id: UUID, optional, references locations.id
- position_in_location: integer, optional
- acquired_date: date, optional
- acquired_where: text, optional
- condition: text, optional
- borrowed_from: text, optional
- lent_to: text, optional
- notes: text, optional
```

```text
locations
- id: UUID, primary key
- name: text
- type: text
- parent_location_id: UUID, optional, references locations.id
- sort_order: integer, optional
- visual_x: decimal, optional
- visual_y: decimal, optional
- visual_z: decimal, optional
- visual_width: decimal, optional
- visual_height: decimal, optional
- visual_depth: decimal, optional
- description: text, optional
- notes: text, optional
```

Beispiele für `locations.type`:

```text
Zimmer
Regal
Fach
Reihe
Stapel
Kiste
Tische
```

```text
series
- id: UUID, primary key
- name: text
- original_title: text, optional
- description: text, optional
- notes: text, optional
```

```text
work_series
- work_id: UUID, references works.id
- series_id: UUID, references series.id
- season: integer, optional
- position: decimal, optional
- position_label: text, optional
- notes: text, optional

primary key:
- work_id + series_id
```

```text
contributors
- id: UUID, primary key
- name: text
- sort_name: text, optional
- birth_year: integer, optional
- death_year: integer, optional
- notes: text, optional
```

```text
work_contributors
- work_id: UUID, references works.id
- contributor_id: UUID, references contributors.id
- role: text
- contributor_order: integer, optional

primary key:
- work_id + contributor_id + role
```

```text
edition_contributors
- edition_isbn: text, references editions.isbn
- contributor_id: UUID, references contributors.id
- role: text
- contributor_order: integer, optional

primary key:
- edition_isbn + contributor_id + role
```

```text
genres
- id: UUID, primary key
- name: text
- parent_genre_id: UUID, optional, references genres.id
- description: text, optional
- notes: text, optional
```

```text
work_genres
- work_id: UUID, references works.id
- genre_id: UUID, references genres.id
- primary_genre: boolean, optional

primary key:
- work_id + genre_id
```

```text
work_reading_status
- work_id: UUID, references works.id
- language_code: text
- status: text
- started_date: date, optional
- finished_date: date, optional
- rating: integer, optional
- notes: text, optional

primary key:
- work_id + language_code
```

Die Beziehungen:

```text
works 1:n editions
editions 1:n copies

copies n:1 locations
locations 1:n locations
über parent_location_id

works n:m series
über work_series

works n:m contributors
über work_contributors

editions n:m contributors
über edition_contributors

works n:m genres
über work_genres

genres 1:n genres
über parent_genre_id

works 1:n work_reading_status
ein Status pro Werk + Sprache
```

Typische Hierarchie für Locations:

```text
Schlafzimmer
└── Regal 1
    └── Fach A
        ├── Erste Reihe
        └── Zweite Reihe

Wohnzimmer
└── Stapel neben der Kommode
```

Typische Genre-Hierarchie:

```text
Fantasy
├── High Fantasy
├── Urban Fantasy
└── Dark Fantasy

Science Fiction
├── Space Opera
├── Cyberpunk
└── Dystopia
```
