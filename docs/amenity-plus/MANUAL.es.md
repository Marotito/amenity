# Amenity+ · Manual de Facility

**Versión de prueba:** 2.0.1-dev25 · **Aparato:** M-VAVE FM-1 · **Rama:** `amenity-plus`

> ¿Primera vez? Empezá por la [Guía de prueba desde cero](TESTING.es.md): explica todo sin conocimientos
> previos. Este manual es la referencia técnica resumida.

Amenity+ es un fork de [Amenity](https://github.com/ejtb76/amenity) (Presethead), el firmware de breaks en vivo
para el FM-1. Mantiene todo Amenity y le suma **Facility**: un multitrack generativo de cuatro pistas que toca
con el break.

| Pista | Qué hace |
|---|---|
| **T1 BREAK** | El break de Amenity, con todos sus efectos en vivo. |
| **T2 BASS** | Bajo generativo, acoplado al bombo y a los acordes. |
| **T3 HARMONY** | Armonía: un pad y una capa rítmica (skank, comping, stabs). |
| **T4 MELODY** | Melodía generativa a partir de motivos, que escucha la armonía y respeta el break. |

Las cuatro pistas siguen un **estilo** (BREAKCORE, JAZZ, ACID, HIP HOP, DUB) y, si querés, una **forma de tema**
(intro, verso, estribillo, puente, breakdown...). Todo arranca en **AUTO**: el estilo elige modos, sonidos,
groove y efectos. Lo que toques pasa a mandar.

---

## 1. Instalar y volver atrás

- **Instalador:** https://marotito.github.io/amenity/install/ (Chrome o Edge, FM-1 por USB con un cable de datos).
- **Emulador** (para probar sin el aparato): https://marotito.github.io/amenity/emu/
- **Tus breaks:** https://marotito.github.io/amenity/breaks/
- **Volver al firmware de M-VAVE:** desde el mismo instalador, *Back to the stock firmware*.

> Ojo: los enlaces del README original (`ejtb76.github.io`) instalan el Amenity original, **sin Facility**.

**Si el instalador falla.** A veces el modo loader entra y después falla la carga. Hacé esto:
1. Desconectá el USB.
2. Volvé a conectarlo.
3. Apretá **Install** otra vez: la instalación retoma donde quedó.

Para que no pase, cerrá Ableton y toda app MIDI que pueda tomar el puerto, y conectá el FM-1 directo, sin hub.

---

## 2. Las dos páginas

**GLO** alterna entre la página **Amenity** (el break) y la página **FACILITY** (en rojo arriba).

Estos botones funcionan en las dos páginas:

| Control | Qué hace |
|---|---|
| **HOME** | Mute / unmute del **break**. |
| **SAVE** | Mute / unmute del **bajo**. |
| **ARP** | Mute / unmute de la **armonía**. |
| **SEQ** | Mute / unmute de la **melodía**. |
| **SEL** + mute | El mute entra al final de la frase (2 compases) y no al instante. |
| **LFO** | Fills automáticos del break (en Amenity original estaba en SEQ). |
| **EDIT + HOME** | Break derecho: variación y warp a cero. |
| **EDIT** (mantener) | Medidor de CPU (ver §8). |
| **EDIT + GLO** | Test de CPU (ver §8). |
| **ENV** (mantener) + tecla blanca | Slots (ver §7). |

La página **Amenity** funciona como en el original (rolls en las blancas, golpes en las negras, K1 FEEDBACK,
K2 VARIATION, K3 WARP, K4 FILTER, EQ con ENV, FX del break con FX). Ver el [README](../../README.md).

---

## 3. Página FACILITY

**SELECT** recorre el **foco**: BASE → BREAK → BASS → HARMONY → MELODY. Las perillas y los encoders actúan sobre
el foco. La línea de abajo de la pantalla dice qué hace cada uno.

### BASE (el tema)

| Control | Qué hace |
|---|---|
| **ALGO** | Estilo: BREAKCORE, JAZZ, ACID, HIP HOP, DUB. |
| **PRESET** | Progresión de acordes: AUTO o una de las 4 del estilo. |
| **K1 ENERGY** | Cuánto tocan las pistas (densidad). |
| **K2 CHORDS** | Velocidad del cambio de acorde: SLOW, AS IS, FAST. |
| **K3 AUTO DUB** | Qué tan seguido una pista sale un compás o dos (OFF para nunca). |
| **K4 FORM** | OFF: loop. ON: forma de tema. +BRK: forma, y además mutea el break y pide fills en los cambios. |

Arriba a la derecha se ven el acorde, la sección y su compás (p. ej. `CHORUS 3/8`) y la progresión.
`A:` antes de un valor indica que lo elige el estilo (AUTO).

### BREAK

| Control | Qué hace |
|---|---|
| **PRESET** | Cambia de break. |
| **K1 LEVEL** | Nivel del break dentro de la mezcla. |
| **K2 / K3 / K4** | VARIATION, WARP, FILTER, igual que en la página Amenity. |

### BASS · HARMONY · MELODY

| Control | Qué hace |
|---|---|
| **ALGO** | Modo (cómo toca). AUTO o uno fijo (ver §4). |
| **PRESET** | Sonido. AUTO, los sonidos propios y después las voces del banco DX7 de fábrica (ver §5). |
| **SEL + ALGO** | Estilo propio de la pista (`= BASE` sigue al tema). Ejemplo: tema DUB con melodía JAZZ. |
| **SEL + PRESET** | Solo en MELODY: la **semilla** (el motivo base, ver §4). |
| **K1 LEVEL** | Nivel de la pista. |
| **K2–K4 en BASS** | CUTOFF, RESO, SLIDE. |
| **K2–K4 en HARMONY** | BRIGHT, MOVEMENT, DECAY. |
| **K2–K4 en MELODY** | BRIGHT, GLIDE, VIBRATO. |

En el centro, 0 en CUTOFF/RESO/BRIGHT/MOVEMENT/DECAY significa "como lo arma el sonido".

La velocidad de cada pista (mitad, normal, doble) se ajusta sola según el tempo y el estilo. La línea de abajo
muestra `HALF SPEED` o `DOUBLE SPEED` cuando no es la normal.

### Groove (SEL mantenido)

| Foco | SEL + K1 | SEL + K2 |
|---|---|---|
| **BASE** | SWING del tema: AUTO o 50–75 %. | HUMAN: AUTO o 0–100. |
| **BASS, HARMONY** | Swing propio (`=` sigue al tema). | ACENTOS. |
| **MELODY** | Swing propio (en AUTO, swing de solista). | EXPRESIÓN (arco de la frase). |
| **BREAK** | Swing del break (arranca en OFF). | — |

Swing y humanize AUTO por estilo:

| | BREAKCORE | JAZZ | ACID | HIP HOP | DUB |
|---|---|---|---|---|---|
| Swing | 50 | 64 | 54 | 58 | 56 |
| Human | 30 | 70 | 25 | 60 | 50 |

---

## 4. Modos y semillas

| Pista | Modos |
|---|---|
| **BASS** | KICK (sigue al bombo), LONG (notas largas), RAGGA, ACID (con slides y acentos), WALK (walking de jazz), 1 DROP (one drop de dub), RIFF. |
| **HARMONY** | SKANK (contratiempo), JAZZ (comping), MIX, STABS, PAD, MIX+PAD. |
| **MELODY** | SEED (desarrolla una semilla), RIFF (motivo corto repetido), LINE (línea continua), ANSWER (pregunta y respuesta). |

**Semillas de la melodía** (dominio público, reescritas en 4/4 para que encajen con el break):
DIES IRAE, TOCCATA, FIFTH, MOUNTAIN KING, FUR ELISE, MINUET, GREENSLEEVES, GYMNOPEDIE, BADINERIE, ENTERTAINER.

La melodía desarrolla la semilla según la sección: respuesta, secuencia, inversión, aumentación en el breakdown.
Las notas fuertes caen en notas del acorde o en tensiones, y las notas largas evitan la caja del break.

---

## 5. Sonidos

PRESET recorre primero los sonidos propios y después las voces del **banco DX7 de fábrica** del FM-1. El nombre
de la voz aparece en pantalla.

| Pista | Propios | Del banco |
|---|---|---|
| **BASS** | SUB, ACID, REESE, FINGER, FM BASS, DUB | 16 voces |
| **HARMONY** | ORGAN, SAW, EPIANO, CLAVI, HARPSI, GUITAR, BRASS | 56 voces |
| **MELODY** | SAW, SQUARE, FM BELL, REED, STRING, FLUTE, VIBES, PLUCK, BRASS | 38 voces |

Los sonidos FM usan el motor DX7 (msfa/Dexed, en el port de Felucca). Hay hasta 8 voces FM a la vez: 3 del pad,
3 de la capa rítmica, el bajo y la melodía.

---

## 6. Efectos de Facility (FX en la página FACILITY)

En la página FACILITY, **FX** abre la vista de envíos (LED de FX prendido). FX otra vez la cierra. En la página
Amenity, FX sigue siendo el de los efectos del break.

**SEL + perilla** vuelve el valor a AUTO.

| Foco | ALGO / PRESET | K1 | K2 | K3 | K4 |
|---|---|---|---|---|---|
| **BASE** | ALGO: reverb AUTO / ROOM / SPRING. PRESET: tiempo del echo (AUTO, 1/8, 3/16, 1/4, 3/8, 1/2, libre 100–1450 ms). | FEEDBACK del echo | COLOR | DECAY de la reverb | RETURN |
| **BREAK** | — | Envío al echo | Envío a la reverb | THROWS (la última caja de la frase al echo) | — |
| **BASS / HARMONY / MELODY** | — | Envío al echo | Envío a la reverb | CHORUS | PAN |

El echo es de cinta: se oscurece y satura en cada vuelta. Arriba de 100 de feedback autooscila y al STOP se
suelta. Los envíos del break arrancan en 0, así que Amenity sola suena igual que el original.

---

## 7. Slots (escenas)

Hay 16 slots. Cada uno guarda la escena completa: ajustes de Amenity, tempo, BASE, las 4 pistas, mutes,
semilla, groove y efectos.

| Control | Qué hace |
|---|---|
| **ENV + EDIT + blanca** | Guardar en ese slot. Si está tocando, se escribe en la flash al próximo STOP (`SAVED AT STOP`). |
| **ENV + blanca** | Cargar. Tocando, entra en el próximo bloque de 4 compases; parado, al instante. |
| **ENV + SEL + blanca** | Cargar con una semilla nueva: la misma escena con otro tema. |

Escribir en la flash produce un silencio momentáneo. Por eso, guardando mientras toca, se espera al STOP.

---

## 8. CPU: medidor, test y gobernador

**EDIT mantenido** muestra arriba:

- `CPU nn%`: la carga del audio.
- `Bnn Tnn Fnn`: qué parte del bloque de audio usan el **B**reak, las pis**T**as y los e**F**ectos.
- `FM costo/presupuesto !n`: el costo de las voces FM, el presupuesto del gobernador, y cuántas veces un bloque
  pasó el 90 % desde que se encendió.

**EDIT + GLO** activa o desactiva el **test de CPU**: las 8 voces FM sostenidas con el sonido más caro y todos los
efectos al máximo. Es el peor caso posible.

**El gobernador.** Cuando la carga se acerca al límite, las voces FM de la armonía ceden su lugar. Primero cede
el pad, empezando por las notas que ya se están apagando, con un fundido de 8 ms. El bajo y la melodía no se
cortan nunca. Si un bloque pasa el 90 %, el presupuesto baja, y solo vuelve a crecer cuando la carga está por
debajo del 70 %. Con sonidos FM pesados en todas las pistas puede faltar alguna nota del pad. Es lo previsto:
mejor eso que un corte de audio.

**Si aparece `AUDIO OVERLOAD`**, el audio se cortó. Eso no debería pasar: reportalo (ver
[TESTING.es.md](TESTING.es.md)).

Referencia medida en el aparato (dev25, test de CPU, Amenity con feedback 100 + stretch): **74 % de pico**,
B19 T43 F11, FM 44/44, sin cortes.

---

## 9. Problemas conocidos (dev25)

- **`!` suma 2 al arrancar cada tema.** Es un pico de cálculo de un solo bloque, al generar la canción. No corta
  el audio. Se va a repartir en varios bloques en la próxima versión.
- **Pop al encender.** Viene de la plataforma (fm1-base), igual que en Amenity original. Se analiza aparte, sin
  tocar el arranque hasta entender cómo lo hace el firmware de fábrica.
- **Melodías.** Falta un pulido de frases y desarrollo. Las observaciones de oído son bienvenidas.
- **Sonidos AUTO del banco.** Se eligieron por nombre y todavía no se ajustaron de oído.

---

## 10. Créditos y licencias

- **Amenity:** Presethead, GPL-3.0-only.
- **fm1-base y felucca-bsp:** Charles Vestal y Leo Kuroshita (Hügelton Instruments), MPL-2.0.
- **Motor FM6:** msfa (Dexed) en el port entero de Felucca, Apache-2.0.
- **Banco DX7 de fábrica:** M-VAVE, desde fm1_open, Apache-2.0.
- **Facility (Amenity+):** Marote, sobre Amenity, GPL-3.0-only.
- Los breaks de muestra son audio de terceros: ver el [README](../../README.md#third-party-audio).

Amenity+ no está afiliado a M-VAVE ni a Presethead.
