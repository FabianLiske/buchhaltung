# buchhaltung

Lokale Terminal-Anwendung zur Verwaltung einer Buechersammlung.

## Build

Das Repository kann auf einem nicht ausfuehrbaren SMB-Mount liegen. Deshalb
muessen alle Build-Artefakte ausserhalb des Repositorys erzeugt werden. Der
mitgelieferte CMake-Preset nutzt dafuer `~/build/buchhaltung`.

Clean konfigurieren:

```sh
rm -rf "$HOME/build/buchhaltung"
cmake --preset local
```

Bauen:

```sh
cmake --build --preset local
```

Parallel mit allen CPU-Threads bauen:

```sh
cmake --build --preset local --parallel "$(nproc)"
```

Testen:

```sh
ctest --preset local
```

Das erzeugte Programm liegt danach unter:

```sh
"$HOME/build/buchhaltung/buch"
```

Direkte CMake-Variante ohne Preset:

```sh
rm -rf "$HOME/build/buchhaltung"
cmake -S . -B "$HOME/build/buchhaltung"
cmake --build "$HOME/build/buchhaltung" --parallel "$(nproc)"
ctest --test-dir "$HOME/build/buchhaltung" --output-on-failure
```
