# Road Rash Music Player

[![Descargar](https://img.shields.io/github/v/release/FFNNO/RoadRashMusicPlayer?label=Descargar&style=for-the-badge)](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/FFNNO/RoadRashMusicPlayer/release.yml?branch=main&style=for-the-badge)](https://github.com/FFNNO/RoadRashMusicPlayer/actions/workflows/release.yml)

[![Twitter/X @PagaTeamDev](https://img.shields.io/badge/X-%40PagaTeamDev-000000?logo=x&style=for-the-badge)](https://x.com/PagaTeamDev)
[![Donar en Cafecito](https://img.shields.io/badge/Donar-Cafecito-0ea5e9?logo=buymeacoffee&logoColor=white&style=for-the-badge)](https://cafecito.app/pagateam)

[Read in English](README.md)

Reproductor de música integrado en **Road Rash 95** (Windows, versión de Steam). La música propia se coloca en una carpeta y el juego la reproduce en carreras y menús, con teclas rápidas, una fila `Reproductor` en el menú de opciones del juego y un panel en pantalla, el "beeper", que muestra el casete y la pista.

Es un `dsound.dll` proxy: se ubica junto a `RoadRash95.exe`, reenvía cada llamada de DirectSound al `dsound.dll` real de Windows y agrega el reproductor. No modifica ningún archivo del juego. Al borrar la DLL, el juego vuelve a su estado original.

**Es un mod creado por fans, sin relación con Electronic Arts ni respaldo de su parte.** No incluye ni distribuye archivos del juego. Se necesita una copia propia del juego.

## Contenido

- [Requisitos](#requisitos)
- [Instalación](#instalación)
- [La carpeta music](#la-carpeta-music)
- [Teclas rápidas](#teclas-rápidas)
- [La fila Reproductor y su ventana](#la-fila-reproductor-y-su-ventana)
- [El beeper](#el-beeper)
- [Configuración: rr95music.ini](#configuración-rr95musicini)
- [Valores del registro](#valores-del-registro)
- [Uso junto a otros mods](#uso-junto-a-otros-mods)
- [Desinstalación](#desinstalación)
- [Solución de problemas](#solución-de-problemas)
- [Limitaciones conocidas](#limitaciones-conocidas)
- [Compilación desde el código fuente](#compilación-desde-el-código-fuente)
- [Licencia](#licencia)

## Requisitos

- Windows 10 u 11.
- **Road Rash 95 en Steam** (30th Anniversary Edition, app 2448570).
- Ningún otro mod. El reproductor funciona de forma independiente, y esa es la configuración recomendada. Usarlo junto a otros mods que modifican las mismas rutinas del juego, como **Road Rash FFNNO Patch** (<https://github.com/FFNNO/RoadRashFFNNOPatch>), es experimental: ver [Uso junto a otros mods](#uso-junto-a-otros-mods).

## Instalación

### Con el instalador

1. Descargar `RoadRashMusicPlayer-Setup-<versión>.exe` desde la [última versión publicada](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest). Cada versión se compila con GitHub Actions a partir del commit etiquetado e indica el SHA-256 de cada archivo.
2. Cerrar el juego y ejecutar el instalador. Encuentra la carpeta del juego a partir del registro o de las bibliotecas de Steam, y solo continúa si `RoadRash95.exe` está en la carpeta elegida. Si no puede escribir allí, debe ejecutarse como administrador.
3. Instala `dsound.dll`, agrega `rr95music.ini` solo si la carpeta no tiene uno (la configuración existente nunca se sobrescribe) y crea la carpeta `music`. La tarea sin marcar "Experimental: funcionar junto a otros mods" escribe `OtherModsCompat=1` solo en ese `rr95music.ini` nuevo; para el modo independiente debe quedar sin marcar.

Si la carpeta del juego ya tiene un `dsound.dll` de otro mod, el instalador lo advierte, guarda ese archivo como `dsound.dll.bak-before-musicplayer` (o con ese nombre más la fecha y la hora, si ya existe una copia con ese nombre) y lo restaura al desinstalar. Nunca se sobrescribe una copia de seguridad.

Instalación silenciosa: `RoadRashMusicPlayer-Setup-0.1.1.exe /VERYSILENT /CURRENTUSER /DIR="<carpeta del juego>"`. Con `/TASKS="othermodscompat"` se selecciona la tarea experimental.

### Manual

1. Cerrar el juego.
2. Descargar `RoadRashMusicPlayer-<versión>.zip` desde la [última versión publicada](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest) y copiar `dsound.dll` junto a `RoadRash95.exe` (normalmente `C:\Program Files (x86)\Steam\steamapps\common\RoadRash`).
3. Opcional: copiar `rr95music.ini` junto a la DLL y ajustar lo necesario. Cada clave tiene un valor predeterminado, así que el reproductor funciona sin el archivo.
4. Iniciar el juego. La carpeta `music` se crea en el primer inicio y `rr95music.log` indica qué se cargó.

| Archivo | Contenido |
|---|---|
| `dsound.dll` | el reproductor (32 bits) |
| `rr95music.ini` | configuración opcional (ver más abajo) |
| `rr95music.log` | el registro del reproductor, en la carpeta del juego |
| `music\` | los casetes, creada al iniciar si no existe |

## La carpeta music

Cada subcarpeta de primer nivel de `music\` es un **casete** con el nombre de la carpeta; los archivos de carpetas más profundas pertenecen a su casete de primer nivel. Los archivos de audio sueltos directamente en `music\` forman un casete llamado `music`. La banda sonora del propio juego es el casete `Road Rash`; aparece primero y sigue la elección del jukebox del juego.

Formatos admitidos: `.wav`, `.mp3`, `.ogg` y `.flac`. El título y el artista se leen de las etiquetas ID3v1/v2, Vorbis, FLAC y RIFF `INFO`.

```
RoadRash\
  RoadRash95.exe
  dsound.dll
  rr95music.ini
  music\
    Driving song.mp3          <- el casete "music"
    Grunge\                   <- el casete "Grunge"
      01 First track.flac
      02 Second track.ogg
```

`Path` en `rr95music.ini` cambia la carpeta. `Shuffle` y `Volume` se aplican a todos los casetes.

## Teclas rápidas

| Acción | Tecla predeterminada | Clave del ini |
|---|---|---|
| Pista siguiente | `PageDown` (Av Pág) | `NextKey` |
| Pista anterior | `PageUp` (Re Pág) | `PrevKey` |
| Reproducir / pausar | `End` (Fin) | `PlayPauseKey` |
| Casete siguiente | `Insert` (Insert) | `NextCassetteKey` |
| Mostrar u ocultar el beeper en la carrera actual | `Home` (Inicio) | `OverlayKey` |

Las teclas predeterminadas no las usa el juego. Se cambian en la ventana del Reproductor (ver [Captura de teclas](#captura-de-teclas)) o en `rr95music.ini` con la sintaxis `Ctrl+Shift+Alt+Tecla`; los modificadores son opcionales y pueden ir en cualquier orden. Una tecla igual a la `ToggleKey` del mod general (de `rr95gpu.ini`, `ScrollLock` si ese archivo no existe) o a `F11` queda desactivada.

## La fila Reproductor y su ventana

El menú de opciones de gráficos y sonido del juego incorpora una fila `Reproductor`, que abre una ventana:

| Fila | Función |
|---|---|
| `Cassette` | Cambia al casete siguiente; la elección se conserva para las próximas carreras. |
| `Beeper` | Alterna entre `Fijo` y `A demanda`. |
| `Siguiente`, `Anterior`, `Pausa`, `Cambiar cassette`, `Mostrar beeper` | Muestran la tecla actual de cada acción; al seleccionar una se captura una tecla nueva. |
| `Volver` | Cierra la ventana. |

### Captura de teclas

Seleccionar una fila de tecla y pulsar la tecla nueva. `Enter` o `Esc` cancelan y mantienen la tecla anterior. Si otra acción ya usa esa tecla, las dos acciones intercambian sus teclas. Se rechazan las teclas reservadas (`Enter`, `Esc`, `Tab`, las flechas, la tecla de alternancia del mod general y `F11`) y los modificadores solos. El resultado se guarda en el registro (`ModMusicKeys`) para el siguiente inicio.

## El beeper

El beeper es el panel del reproductor en pantalla: el casete, la pista y el artista, dibujados sobre la imagen del juego en la esquina elegida (`Corner`).

- `A demanda` / `Overlay=ondemand` (predeterminado): el panel aparece unos segundos después de cada cambio de pista.
- `Fijo` / `Overlay=pinned`: el panel permanece en pantalla durante toda la carrera.

La tecla del beeper (`Home`) muestra u oculta el panel solo en la carrera actual; el modo guardado no cambia. Los títulos largos se desplazan.

## Configuración: rr95music.ini

`rr95music.ini` se ubica junto a `RoadRash95.exe`. El reproductor no lee la configuración de `rr95gpu.ini`, salvo la `ToggleKey` del mod general, y nunca escribe en `rr95gpu.log`.

| Clave | Predeterminado | Función |
|---|---|---|
| `Enabled` | `1` | `1` = cargar el reproductor. `0` = solo reenviar las llamadas de DirectSound: sin hooks. |
| `OtherModsCompat` | `0` | **Experimental.** `0` = modo independiente (recomendado). `1` = encadenarse a los hooks que otros mods ya instalaron en las mismas rutinas del juego; ver [Uso junto a otros mods](#uso-junto-a-otros-mods). |
| `LogLevel` | `info` | `debug`, `info`, `warn` o `error`; cualquier otro valor usa `info`. |
| `Path` | `music` | Carpeta de casetes, relativa a la carpeta del juego o absoluta. Se crea al iniciar si no existe. |
| `Shuffle` | `0` | `1` = orden aleatorio de las pistas de cada casete; `0` = orden por nombre. |
| `Volume` | `80` | `0..100`. Los valores fuera de rango se ajustan al límite; un valor no numérico mantiene 80. |
| `NextKey` | `PageDown` | Pista siguiente. |
| `PrevKey` | `PageUp` | Pista anterior. |
| `PlayPauseKey` | `End` | Reproducir o pausar. |
| `NextCassetteKey` | `Insert` | Casete siguiente. |
| `OverlayKey` | `Home` | Mostrar u ocultar el beeper en la carrera actual. |
| `Overlay` | `ondemand` | Modo del beeper: `ondemand` o `pinned`. |
| `Corner` | `bottomleft` | Esquina del panel: `bottomleft`, `topleft`, `bottomright` o `topright`. |

## Valores del registro

El reproductor guarda las elecciones hechas en el juego en `HKCU\SOFTWARE\Electronic Arts\RoadRash 95`. Al iniciar, un valor válido tiene prioridad sobre la clave equivalente del ini.

| Valor | Tipo | Contenido |
|---|---|---|
| `ModMusicCassette` | `REG_SZ` | el casete elegido (`Road Rash` para la banda sonora del juego) |
| `ModMusicKeys` | `REG_BINARY` | versión 1 más un byte de código de tecla virtual por acción |
| `ModMusicOverlay` | `REG_BINARY` | un byte: `1` = fijo, `0` = a demanda |

## Uso junto a otros mods

### Modo independiente (predeterminado, recomendado)

**El modo independiente es el modo predeterminado y estable, y es la configuración recomendada.** El reproductor instala sus propios hooks en las rutinas del juego que necesita, cada uno solo después de verificar los bytes originales del juego: la presentación de la carrera y del menú, el texto de los elementos del menú, la pausa y reanudación de la música nativa, el inicio de la música de carrera y la entrada del menú que usa la captura de teclas. Escribe la fila `Reproductor` en el menú de gráficos del propio juego (en una posición sin uso) y mezcla el beeper con la imagen del juego. No necesita ningún otro mod.

Si otro mod ya modificó una de esas rutinas, los bytes originales ya no están, así que el reproductor no toca esa rutina y la parte del reproductor que la necesita queda desactivada. Por ejemplo, junto a Road Rash FFNNO Patch v0.1.3 el reproductor queda en silencio, porque ese mod ya modifica la presentación de la carrera, la del menú y el texto de los elementos del menú.

### Experimental: `OtherModsCompat=1`

`OtherModsCompat=1` en `rr95music.ini` activa un modo **experimental** que encadena el reproductor a los hooks que otros mods ya instalaron. Si una rutina empieza con un salto de otro mod hacia su propio código, el reproductor coloca su propio salto delante: primero se ejecuta la parte del reproductor y después el código del otro mod, que sigue llegando al código original del juego. Una rutina modificada de cualquier otra forma no se toca. El instalador puede escribir este valor en un `rr95music.ini` nuevo con la tarea sin marcar "Experimental: funcionar junto a otros mods".

- **Probado con Road Rash FFNNO Patch v0.1.3.** Se espera que funcionen la música y las teclas rápidas. El beeper y la fila `Reproductor` pueden no estar disponibles junto a un mod de GPU: si el otro mod dibuja el juego por su cuenta (por ejemplo con Direct3D), el beeper no puede dibujarse, y si mueve el menú de gráficos, la fila se omite. `rr95music.log` lo indica una vez. No se probaron otros mods ni otras versiones.
- **Riesgos:** depende de cómo el otro mod mantiene sus hooks. Si ese mod los quita o los cambia, el reproductor puede perder esos hooks: los revisa una vez por segundo, registra una sola vez cada hook perdido y deja de usarlo, sin volver a instalarlo. Es posible que el juego se cierre de forma inesperada.
- **Desactivarlo:** escribir `OtherModsCompat=0` en `rr95music.ini` (o borrar la línea) y reiniciar el juego.

### El `dsound.dll` de otro mod

Solo puede haber un archivo llamado `dsound.dll` en la carpeta del juego, así que un mod que también es un `dsound.dll` entra en conflicto con el reproductor por el nombre del archivo, en cualquiera de los dos modos. El instalador guarda ese archivo aparte y lo restaura al desinstalar (ver [Instalación](#instalación)).

[`include/rr95_mod_api.h`](include/rr95_mod_api.h) es una interfaz interna sin uso: el código de enlace inactivo del reproductor todavía se compila con ella, pero el reproductor nunca se conecta a través de ella.

## Desinstalación

- **Instalado con el instalador:** desinstalar "Road Rash Music Player" desde Aplicaciones y características (Configuración > Aplicaciones). Elimina `dsound.dll` y restaura el `dsound.dll` de otro mod que el instalador haya guardado.
- **Instalado manualmente:** borrar `dsound.dll` de la carpeta del juego.

En ambos casos se conservan la carpeta `music`, `rr95music.ini`, `rr95music.log` y los valores del registro; pueden borrarse a mano si ya no se necesitan. El mod general nunca se modifica.

## Solución de problemas

- **No ocurre nada en el juego:** abrir `rr95music.log` en la carpeta del juego. Indica si el reproductor se cargó, el valor de `OtherModsCompat`, qué hooks instaló, encadenó o rechazó, y qué casetes leyó. Una línea sobre una versión desconocida del ejecutable significa que el ejecutable del juego cambió: en ese caso el reproductor solo reenvía las llamadas de DirectSound. Con `LogLevel=debug` se obtiene más detalle.
- **El reproductor queda en silencio junto a otro mod:** el registro muestra un `byte mismatch` por cada rutina que ese mod ya modificó. El modo independiente no toca esas rutinas. Se puede probar el modo experimental `OtherModsCompat=1` (ver [Uso junto a otros mods](#uso-junto-a-otros-mods)) o usar el reproductor sin el otro mod.
- **Algo falla con `OtherModsCompat=1`:** escribir `OtherModsCompat=0`, reiniciar el juego e informar el problema adjuntando `rr95music.log`.
- **No suena la música de la carpeta:** revisar `Path` y los formatos de archivo. Cada subcarpeta de primer nivel de `music\` es un casete; los archivos de carpetas más profundas pertenecen a su casete de primer nivel, y un casete sin archivos admitidos se omite.
- **Otro mod también usa `dsound.dll`:** solo puede haber un archivo con ese nombre en la carpeta del juego, así que solo uno de los dos mods puede estar activo. El instalador guarda el otro archivo como `dsound.dll.bak-before-musicplayer` (más la fecha y la hora si ese nombre ya está en uso); al desinstalar el reproductor se recupera, o pueden intercambiarse los archivos a mano.
- **Una tecla rápida no funciona:** puede coincidir con la `ToggleKey` del mod general o con `F11`, lo que la desactiva; el registro lo indica. Conviene elegir otra tecla.
- **Advertencia del antivirus:** algunos antivirus pueden marcar una DLL proxy porque modifica el código del juego en memoria. No modifica ningún archivo del juego.

## Limitaciones conocidas

- **`ModMusicKeys` guarda solo teclas simples.** Una asignación con `Ctrl`, `Shift` o `Alt` definida en el ini conserva sus modificadores solo durante la sesión actual; el siguiente inicio lee la tecla simple del registro.
- **Es incompatible con cualquier otro mod que también sea un `dsound.dll`.** Solo puede haber uno instalado a la vez.
- **Necesita el ejecutable de Steam que conoce.** Con otra versión del ejecutable, el reproductor queda desactivado y solo reenvía las llamadas de DirectSound.
- **`OtherModsCompat=1` es experimental.** Solo se probó con Road Rash FFNNO Patch v0.1.3, y junto a un mod de GPU el beeper y la fila `Reproductor` pueden no estar disponibles.
- **Es una versión pública de prueba temprana (v0.1.1).** Los problemas pueden informarse adjuntando `rr95music.log`.

## Compilación desde el código fuente

Requisitos: Visual Studio 2026 (VS 18) Community con "Desarrollo para el escritorio con C++" (MSVC x86, Windows SDK, CMake), Python 3 e [Inno Setup 6](https://jrsoftware.org/isinfo.php) para el instalador.

```bat
build.bat
```

compila `build\Release\dsound.dll` y `build\Release\music_test.exe` (Win32, Release, CRT estático) y termina con `build.bat: OK`. Después:

```bat
build\Release\music_test.exe
python -I tools\check_hook_sites.py --original <RoadRash95.exe original> --patched <RoadRash95.exe parcheado>
installer\build.bat
python -I installer\tests\test_installer.py
```

- `music_test.exe` ejecuta las pruebas unitarias y termina con `OK (0 failure(s))`.
- `tools/check_hook_sites.py` verifica cada punto de hook contra copias propias del ejecutable del juego (o con las variables `RR95_ORIGINAL_EXE` y `RR95_PATCHED_EXE`). Los ejecutables nunca forman parte de este repositorio; sin ellos, el script informa `skipped` y termina con código 0.
- `installer\build.bat` compila el reproductor e `installer\Output\RoadRashMusicPlayer-Setup-<versión>.exe`. `APPVERSION` reemplaza la versión, e `ISCC` indica la ruta de Inno Setup si no se encuentra.
- `test_installer.py` instala en carpetas de juego falsas y temporales, y vuelve a desinstalar.

| Ruta | Contenido |
|---|---|
| `src/` | el reproductor (C++17); `src/shared/` contiene el motor de hooks, el registro y la verificación del ejecutable compartidos con el mod general |
| `include/rr95_mod_api.h` | una interfaz interna sin uso; el código de enlace inactivo se compila con ella |
| `third_party/` | dr_libs y stb_vorbis, incluidos sin modificaciones |
| `tests/music_test.cpp` | pruebas unitarias |
| `tools/check_hook_sites.py` | verificador de puntos de hook |
| `installer/` | script de Inno Setup, script de compilación, pruebas e imágenes |
| `rr95music.ini` | plantilla de configuración |

## Licencia

MIT, ver [LICENSE](LICENSE). Código de terceros: ver [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Cambios: ver [CHANGELOG.md](CHANGELOG.md).
