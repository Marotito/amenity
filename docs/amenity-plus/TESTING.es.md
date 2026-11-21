# Amenity+ · Guía de prueba (dev25)

Gracias por probar. Esta guía dice qué probar, qué es normal y cómo reportar. El manual completo está en
[MANUAL.es.md](MANUAL.es.md).

## Antes de empezar

1. Instalá desde **https://marotito.github.io/amenity/install/**. Usá Chrome o Edge, cerrá Ableton y otras
   apps MIDI, y conectá el FM-1 directo, sin hub.
2. Comprobá la versión: el instalador muestra `2.0.1-devNN`. Anotala para el reporte.
3. Si algo sale mal, desde el mismo instalador podés volver al firmware de fábrica con
   **Back to the stock firmware**.

## Qué probar

Marcá lo que probaste y anotá lo raro. Con EDIT mantenido podés ver la CPU y el `FM x/y !n` en cualquier
momento.

### A. Lo básico

- [ ] Encender, PLAY, STOP. El break suena como en Amenity original.
- [ ] GLO cambia entre la página Amenity y FACILITY.
- [ ] Mutes: HOME, SAVE, ARP, SEQ, y con SEL para que entren al final de la frase.
- [ ] LFO activa los fills automáticos.

### B. Estilos y tema

- [ ] En BASE, recorré los 5 estilos con ALGO. Cada uno suena a su estilo.
- [ ] PRESET: las progresiones de cada estilo.
- [ ] K4 FORM en ON y en +BRK: las secciones avanzan (se ve `VERSE 3/8`, etc.) y los cambios tienen sentido.
- [ ] K1 ENERGY, K2 CHORDS, K3 AUTO DUB.

### C. Pistas

- [ ] En BASS, HARMONY y MELODY: ALGO (modos) y PRESET (sonidos propios y voces del banco).
- [ ] SEL + ALGO: estilo propio de una pista.
- [ ] SEL + PRESET en MELODY: las 10 semillas.
- [ ] K2–K4 de cada pista.

### D. Groove y efectos

- [ ] SEL + K1/K2 en BASE y en las pistas: swing, human, acentos, expresión.
- [ ] FX en la página FACILITY: echo (tiempos, feedback alto, autooscilación y STOP), reverb ROOM y SPRING,
      envíos, chorus, pan, throws.

### E. Slots

- [ ] Guardar (ENV + EDIT + blanca) parado y tocando (`SAVED AT STOP`).
- [ ] Cargar parado y tocando (entra a los 4 compases).
- [ ] Cargar con semilla nueva (ENV + SEL + blanca).
- [ ] Apagar, encender y cargar: los slots siguen ahí.

### F. CPU y estabilidad (lo más importante)

- [ ] Tocar 10 minutos o más, cambiando estilos, modos y sonidos (los del banco también). **No debe aparecer
      `AUDIO OVERLOAD`.**
- [ ] Test de CPU: EDIT + GLO, con Amenity a fondo (feedback 100 + stretch). Anotá CPU, `B T F` y `FM x/y !n`
      después de 2 minutos.
- [ ] Con MIDI clock externo (USB, Bluetooth o TRS): Facility sigue el tempo.

## Qué es normal

- `!` sube 2 al arrancar cada tema (problema conocido, no corta el audio).
- Con sonidos FM pesados en todas las pistas, el pad puede perder alguna nota: es el gobernador cuidando el
  audio.
- Un silencio corto al guardar un slot parado (se escribe en la flash).
- El pop al encender (viene de la plataforma).

## Cómo reportar

Abrí un issue en https://github.com/Marotito/amenity/issues con la plantilla **Reporte de error**, o mandá lo
mismo por mensaje. Lo más útil es:

1. **Versión** (`2.0.1-devNN`).
2. **Qué estabas haciendo:** página, foco, estilo, modo y sonido de cada pista, si había clock externo.
3. **Qué pasó** y **qué esperabas**.
4. **Si se puede repetir:** los pasos.
5. **CPU** con EDIT mantenido: `CPU`, `B T F`, `FM x/y !n`.
6. Si es de sonido, **una grabación corta** (el FM-1 manda audio por USB) o una foto de la pantalla.

Las observaciones musicales también valen ("en DUB la melodía termina raro las frases", "este sonido del banco
queda mejor para HIP HOP"). Anotá estilo, pista y sonido.
