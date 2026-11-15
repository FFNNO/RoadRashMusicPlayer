# Road Rash Music Player

[![Descargar](https://img.shields.io/github/v/release/FFNNO/RoadRashMusicPlayer?label=Descargar&style=for-the-badge)](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/FFNNO/RoadRashMusicPlayer/release.yml?branch=main&style=for-the-badge)](https://github.com/FFNNO/RoadRashMusicPlayer/actions/workflows/release.yml)

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
- [Con y sin el mod general](#con-y-sin-el-mod-general)
- [Desinstalación](#desinstalación)
- [Solución de problemas](#solución-de-problemas)
- [Limitaciones conocidas](#limitaciones-conocidas)
- [Compilación desde el código fuente](#compilación-desde-el-código-fuente)
- [Licencia](#licencia)

## Requisitos

- Windows 10 u 11.
- **Road Rash 95 en Steam** (30th Anniversary Edition, app 2448570).
- Opcional: el mod general **Road Rash FFNNO Patch** (<https://github.com/FFNNO/RoadRashFFNNOPatch>), un `ddraw.dll` proxy con renderizado por GPU, pantalla ancha y más. El reproductor funciona sin él. **Para usar ambos a la vez, el mod general debe ser v0.1.4 o posterior.**

## Instalación

### Con el instalador

1. Descargar `RoadRashMusicPlayer-Setup-<versión>.exe` desde la [última versión publicada](https://github.com/FFNNO/RoadRashMusicPlayer/releases/latest). Cada versión se compila con GitHub Actions a partir del commit etiquetado e indica el SHA-256 de cada archivo.
2. Cerrar el juego y ejecutar el instalador. Encuentra la carpeta del juego a partir del registro o de las bibliotecas de Steam, y solo continúa si `RoadRash95.exe` está en la carpeta elegida. Si no puede escribir allí, debe ejecutarse como administrador.
3. Instala `dsound.dll`, agrega `rr95music.ini` solo si la carpeta no tiene uno (la configuración existente nunca se sobrescribe) y crea la carpeta `music`.

Si la carpeta del juego ya tiene un `dsound.dll` de otro mod, el instalador lo advierte, guarda ese archivo como `dsound.dll.bak-before-musicplayer` (o con ese nombre más la fecha y la hora, si ya existe una copia con ese nombre) y lo restaura al desinstalar. Nunca se sobrescribe una copia de seguridad.

Instalación silenciosa: `RoadRashMusicPlayer-Setup-0.1.0.exe /VERYSILENT /CURRENTUSER /DIR="<carpeta del juego>"`.

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
| `Enabled` | `1` | `1` = cargar el reproductor. `0` = solo reenviar las llamadas de DirectSound: sin hooks ni llamadas a la API del mod. |
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

## Con y sin el mod general

**Con Road Rash FFNNO Patch v0.1.4 o posterior**, el reproductor se conecta a través de su API de mods (`RR95_GetModApi`) en lugar de modificar el código del juego que controla el mod general. El mod general dibuja el beeper con su renderizador y muestra la fila `Reproductor` en su menú de gráficos reubicado, junto a sus propias filas de pantalla ancha y vista lejana. El reproductor registra la fila muy temprano, antes de que el mod general construya ese menú.

**Sin el mod general**, el reproductor instala sus propios hooks en las mismas rutinas del juego, cada uno solo después de verificar los bytes originales del juego. Escribe la fila `Reproductor` en el menú de gráficos del propio juego (en una posición sin uso) y mezcla el beeper con la imagen del juego. Todo funciona de la misma manera.

**Con un mod general anterior** (previo a v0.1.4, sin API de mods), ambas DLL necesitarían las mismas rutinas del juego. El reproductor no modifica código que ya está modificado, por lo que parte de sus funciones quedan desactivadas. Se recomienda actualizar el mod general.

El reproductor siempre modifica cuatro rutinas propias (la pausa y reanudación de la música nativa, el inicio de la música de carrera y la entrada del menú que usa la captura de teclas), cada una solo después de verificar sus bytes originales.

El contrato de la API entre ambos mods es [`include/rr95_mod_api.h`](include/rr95_mod_api.h). **Ese archivo pertenece al mod general** (Road Rash FFNNO Patch, `renderer/src/rr95_mod_api.h`) y se copia aquí byte por byte; cualquier cambio debe hacerse primero allí.

## Desinstalación

- **Instalado con el instalador:** desinstalar "Road Rash Music Player" desde Aplicaciones y características (Configuración > Aplicaciones). Elimina `dsound.dll` y restaura el `dsound.dll` de otro mod que el instalador haya guardado.
- **Instalado manualmente:** borrar `dsound.dll` de la carpeta del juego.

En ambos casos se conservan la carpeta `music`, `rr95music.ini`, `rr95music.log` y los valores del registro; pueden borrarse a mano si ya no se necesitan. El mod general nunca se modifica.

## Solución de problemas

- **No ocurre nada en el juego:** abrir `rr95music.log` en la carpeta del juego. Indica si el reproductor se cargó, si encontró el mod general, qué hooks instaló y qué casetes leyó. Una línea sobre una versión desconocida del ejecutable significa que el ejecutable del juego cambió: en ese caso el reproductor solo reenvía las llamadas de DirectSound. Con `LogLevel=debug` se obtiene más detalle.
- **No suena la música de la carpeta:** revisar `Path` y los formatos de archivo. Cada subcarpeta de primer nivel de `music\` es un casete; los archivos de carpetas más profundas pertenecen a su casete de primer nivel, y un casete sin archivos admitidos se omite.
- **Otro mod también usa `dsound.dll`:** solo puede haber un archivo con ese nombre en la carpeta del juego, así que solo uno de los dos mods puede estar activo. El instalador guarda el otro archivo como `dsound.dll.bak-before-musicplayer` (más la fecha y la hora si ese nombre ya está en uso); al desinstalar el reproductor se recupera, o pueden intercambiarse los archivos a mano.
- **Una tecla rápida no funciona:** puede coincidir con la `ToggleKey` del mod general o con `F11`, lo que la desactiva; el registro lo indica. Conviene elegir otra tecla.
- **Advertencia del antivirus:** algunos antivirus pueden marcar una DLL proxy porque modifica el código del juego en memoria. No modifica ningún archivo del juego.

## Limitaciones conocidas

- **`ModMusicKeys` guarda solo teclas simples.** Una asignación con `Ctrl`, `Shift` o `Alt` definida en el ini conserva sus modificadores solo durante la sesión actual; el siguiente inicio lee la tecla simple del registro.
- **Es incompatible con cualquier otro mod que también sea un `dsound.dll`.** Solo puede haber uno instalado a la vez.
- **Necesita el ejecutable de Steam que conoce.** Con otra versión del ejecutable, el reproductor queda desactivado y solo reenvía las llamadas de DirectSound.
- **Es una primera versión pública de prueba (v0.1.0).** Los problemas pueden informarse adjuntando `rr95music.log`.

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
| `include/rr95_mod_api.h` | el contrato de la API de mods, propiedad del mod general |
| `third_party/` | dr_libs y stb_vorbis, incluidos sin modificaciones |
| `tests/music_test.cpp` | pruebas unitarias |
| `tools/check_hook_sites.py` | verificador de puntos de hook |
| `installer/` | script de Inno Setup, script de compilación, pruebas e imágenes |
| `rr95music.ini` | plantilla de configuración |

## Licencia

MIT, ver [LICENSE](LICENSE). Código de terceros: ver [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Cambios: ver [CHANGELOG.md](CHANGELOG.md).
