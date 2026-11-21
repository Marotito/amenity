# Amenity+ · Guía de prueba desde cero

**Versión:** 2.0.1-dev25 · **Aparato:** M-VAVE FM-1

Esta guía es para cualquiera, aunque nunca hayas hecho música ni usado Amenity. No hace falta saber teoría:
cada función se explica por **lo que hace y cómo suena**, y viene con una prueba corta para verificar que
anda.

Cada prueba tiene una casilla `[ ]`. Si algo no hace lo que dice la guía, anotalo: eso es justamente lo que
buscamos (ver [Cómo reportar](#13-cómo-reportar)).

> Hay un manual más técnico y resumido en [MANUAL.es.md](MANUAL.es.md). No hace falta para probar.

---

## 0. Qué es esto, en dos minutos

El FM-1 es un sintetizador chico de M-VAVE. **Amenity+** es un programa (firmware) que se instala en lugar
del de fábrica y lo convierte en una máquina de música en vivo con dos partes:

- **Amenity (el break):** toca un **break**, que es un pedacito de batería grabado de un disco viejo, el que
  se usa en el jungle y el drum and bass. Con las teclas lo podés cortar, repetir, estirar y ensuciar mientras
  suena.
- **Facility (la banda):** encima del break, el aparato **inventa solo** un bajo, unos acordes de fondo y una
  melodía, que siguen al break. Vos elegís el estilo y le das forma con las perillas. No hace falta tocar
  ninguna nota.

### Palabras que vas a ver

No hace falta memorizarlas; volvé acá cuando aparezca alguna.

| Palabra | Qué quiere decir |
|---|---|
| **Break** | Un loop de batería: bombo, caja y platillos que se repiten. |
| **Tempo / BPM** | Qué tan rápido va todo. BPM = golpes por minuto: 85 es tranquilo, 170 es muy rápido. |
| **Pista** | Cada "músico" de la banda: break, bajo, armonía y melodía. |
| **Mute** | Callar una pista sin pararla. Al sacar el mute vuelve justo a tiempo. |
| **Frase** | Un tramo de música que se siente completo, como una oración. Acá dura 2 compases (unos segundos). |
| **Bajo** | Las notas graves, las que se sienten en el pecho. |
| **Armonía / acordes** | El "fondo" de varias notas juntas que le da el clima (alegre, oscuro, tenso). |
| **Melodía** | La parte que se podría silbar. |
| **Tonalidad** | El "centro" alrededor del cual giran todas las notas. Cambiarla cambia el color general. |
| **Filtro** | Saca graves o agudos: como escuchar desde otra habitación, o por un teléfono. |
| **Eco (echo)** | El sonido se repite, cada vez más bajo, como gritar en una montaña. |
| **Reverb** | La sensación de sala: de un cuarto chico a una catedral. |
| **Swing** | El "rengueo" del ritmo: derecho como una marcha o balanceado como el jazz o el hip hop. |
| **AUTO** | El aparato elige solo según el estilo. Al tocar un control, pasa a mandar lo que pusiste. |

---

## 1. Antes de empezar

- [ ] **Instalar.** Abrí **https://marotito.github.io/amenity/install/** en Chrome o Edge en la compu,
      conectá el FM-1 por USB con un cable que transmita datos (no uno solo de carga) y seguí la página. No
      desconectes hasta que diga *Done*.
  - Si se traba a mitad de camino, desconectá, volvé a conectar y apretá **Install** otra vez: sigue donde
    quedó. Ayuda cerrar Ableton u otros programas de música y conectar directo, sin hub.
  - Anotá la versión que muestra la página (`2.0.1-devNN`): la vas a necesitar si reportás algo.
- **Volver al original:** en la misma página, **Back to the stock firmware** reinstala el firmware de M-VAVE.
- **Sin el aparato:** hay un emulador en **https://marotito.github.io/amenity/emu/** (no reemplaza la prueba
  real).
- **Sonido:** auriculares o parlante en la salida del FM-1. Empezá con **MASTER** (el volumen) bajo y subilo de
  a poco.

---

## 2. Conocer el panel

Los controles se nombran con lo que está **impreso en el FM-1**.

| Tipo | Cuáles | Cómo se usan |
|---|---|---|
| **Botones** | PLAY, REC, SEQ, ARP, LFO, ENV, FX, GLO, EDIT, SAVE, HOME, SEL, OCT−, OCT+ | Algunos se **tocan** (apretar y soltar) y otros se **mantienen** mientras hacés otra cosa. |
| **Ruedas** (encoders) | SELECT, ALGORITHM (ALGO), PRESET(S) | Giran sin tope: cada "clic" es un paso. |
| **Perillas** | K1, K2, K3, K4 | Cambian valores. Sus nombres aparecen abajo en la pantalla. |
| **Teclado** | Teclas blancas y negras | Se cuentan **de izquierda a derecha**: "blanca 1" es la primera blanca de la izquierda. |
| **MASTER** | Volumen general | |

**SEL es un modificador**, como la tecla Shift de una computadora: mantenido, cambia lo que hacen otros
controles. Lo mismo pasa con **EDIT**, **ENV** y **FX** cuando los mantenés.

**La pantalla:**
- Arriba, el nombre de la página: **AMENITY** o **FACILITY** (en rojo).
- Abajo, qué hace cada perilla.
- Cuando cambiás algo, arriba aparece un mensaje corto que dice qué cambiaste. Si dudás de qué hizo un
  control, mirá ese mensaje.

---

## 3. Los primeros cinco minutos

Hacelo así la primera vez, sin tocar nada más.

- [ ] **Encender.** Arranca en la página AMENITY, a 160 BPM, con el break AMEN.
- [ ] **PLAY.** Suena el break y el botón se pone verde. Las teclas se van iluminando con cada golpe.
- [ ] **GLO.** Arriba dice FACILITY en rojo. El break sigue y, en uno o dos compases, entran el bajo, los
      acordes y la melodía.
- [ ] Girá **SELECT** hacia la izquierda, de a un clic, hasta que la pantalla diga **FOCUS BASE** (son dos
      clics: arranca en BASS). Ahora girá **ALGO**: el estilo cambia (BREAKCORE, JAZZ, ACID, HIP HOP, DUB) y la
      banda cambia de carácter al toque.
- [ ] Tocá **SAVE**: se calla el bajo. Tocá de nuevo: vuelve.
- [ ] **PLAY** otra vez: para todo.

Si llegaste hasta acá, ya sabés lo básico. Lo que sigue explica cada función.

---

## 4. Página AMENITY: el break

Para llegar: **GLO** hasta que arriba diga AMENITY.

### 4.1 Arrancar, parar, velocidad

| Control | Qué hace | Probá |
|---|---|---|
| **PLAY** | Arranca o para. Verde: tocando. Rojo: suena un efecto. | [ ] Arranca y para. |
| **SELECT** (girar) | Tempo: más rápido o más lento, de a 1 BPM (de a 2 si girás rápido). Va de 40 a 320. | [ ] El break se acelera y se frena. |
| **SEL** + **SELECT** | Tempo fino, de a 0,1. | [ ] Cambia de a poquito. |
| **OCT−** / **OCT+** | Mitad o doble de velocidad, desde el próximo golpe (se pone ámbar mientras espera). | [ ] El ritmo va a la mitad o al doble, sin cortarse. |
| **SEL** + **OCT−** / **OCT+** | Tempo de a 0,1 BPM. | [ ] |

### 4.2 Elegir el break y su tono

| Control | Qué hace | Probá |
|---|---|---|
| **PRESET** (girar) | Cambia de break: AMEN, APACHE, THINK y los tuyos (ver §9). El nuevo entra en el próximo golpe. | [ ] Cambia de batería sin cortar. |
| **ALGO** (girar) | Baja el tono de todo el break, hasta una octava (más grave y pesado). | [ ] El break suena más grave. |

### 4.3 Las teclas blancas: efectos en vivo (mantener)

Mientras **mantenés** una tecla blanca, el break hace un efecto. Al soltar, sigue normal y a tiempo. Si
mantenés varias, manda la última. Probalas con el break sonando.

| Blancas | Efecto | Cómo suena |
|---|---|---|
| **1** | Repetir (lento) | Repite un pedazo, como un disco rayado a ritmo. |
| **2** | Build (subida) | Repite cada vez más rápido: el redoble que "sube" antes de un cambio. |
| **3** | STRETCH | Cada golpe se estira, lento y metálico, como un sampler viejo. |
| **4** | Echo | El sonido se repite cada vez más suave y oscuro. |
| **5, 9, 13** | Repetir | Repiten un pedacito igual: más rápido en 9, muchísimo en 13 (un zumbido). |
| **6, 10, 14** | Repetir bajando | Cada repetición más grave. |
| **7, 11, 15** | Repetir subiendo | Cada repetición más aguda. |
| **8, 12, 16** | Encoger | Cada repetición más corta y más suave, hasta desaparecer. |

Regla fácil: **más a la derecha, más rápido.**

- [ ] Probé las 16 blancas y cada una hace su efecto.
- [ ] Al soltar, el break sigue sin saltos ni silencios.
- [ ] Mantener dos teclas y soltar la última vuelve al efecto de la primera.

### 4.4 Las teclas negras: golpes sueltos y efectos instantáneos

| Negras | Qué hace |
|---|---|
| **1 a 8** (tocar) | Un golpe suelto del break: bombo, caja, bombo, caja, caja, caja, hi-hat, platillo ride. Sirve para "tocar" la batería encima. |
| **9** (mantener) | Feedback: un zumbido metálico, resonante. |
| **10** (mantener) | LO-FI: suena roto y crujiente, como una grabación vieja. |
| **11** (mantener) | Una octava abajo: todo mucho más grave. |

- [ ] Las negras 1–8 dan golpes sueltos. Probá también con el break parado y anotá si suenan o no.
- [ ] Las negras 9, 10 y 11 cambian el sonido mientras las mantenés, y al soltar vuelve todo como estaba.

### 4.5 Las perillas (K1–K4)

| Perilla | Qué hace | Probá |
|---|---|---|
| **K1 FEEDBACK** | Agrega una resonancia metálica que "canta" una nota (0 apagada, 100 al máximo). | [ ] De 0 a 100 el zumbido crece. |
| **K2 VARIATION** | Cuánto cambia el patrón en cada vuelta: golpes cambiados, rellenos, y a veces el **flip** (el break arranca de nuevo a mitad de compás). | [ ] En 0 se repite igual; alta, cada vuelta es distinta. |
| **K3 WARP** | Algunos golpes se estiran (violeta en pantalla) o van a media velocidad y más graves (azul). | [ ] Aparecen golpes violetas y azules y suenan distintos. |
| **K4 FILTER** | A la izquierda, más apagado (sin agudos); a la derecha, más fino (sin graves); en el medio, apagado. | [ ] Barre de oscuro a fino; al cruzar el medio se detiene en "apagado". |

Cuando cambiás VARIATION o WARP, la pantalla dice **NEXT**: el cambio entra en el próximo patrón.

### 4.6 La nota del zumbido y la tonalidad (FX mantenido + tecla)

Mantené **FX** y tocá **cualquier tecla**: elige la nota en la que zumba el feedback (K1). Las teclas
iluminadas muestran la actual.

En Amenity+ hace algo más: **cambia la tonalidad de toda la banda de Facility** desde la próxima frase. Es la
forma más simple de cambiar el color de la música sin saber teoría.

- [ ] Con K1 alto, FX + tecla cambia la nota del zumbido.
- [ ] Con Facility sonando, FX + otra tecla: en la próxima frase bajo, acordes y melodía suenan "en otro
      lugar", todos juntos y sin desafinar.

### 4.7 Botones

| Botón | Qué hace | Probá |
|---|---|---|
| **REC** | Congela el patrón actual: deja de variar (la pantalla dice FROZEN). Tocá de nuevo para soltarlo. | [ ] |
| **LFO** | Fills automáticos: al final de cada frase, un efecto solo (de las blancas o de las negras 9–11). Uno de cada cuatro también barre el filtro. | [ ] Aparecen efectos solos al final de las frases. |
| **EDIT** + **HOME** | Break derecho: variación y warp a cero, el break limpio. | [ ] |
| **HOME / SAVE / ARP / SEQ** | Mutes de las cuatro pistas (ver §5). | |

### 4.8 Página de ecualizador (ENV tocado)

Tocá **ENV** (apretar y soltar) y las perillas pasan a ser un ecualizador: suben o bajan rangos del sonido,
hasta ±12.

| Perilla | Rango |
|---|---|
| **K1** | Graves profundos (el bombo, la panza). |
| **K2** | Medios graves (cuerpo). |
| **K3** | Medios agudos (presencia, la caja). |
| **K4** | Agudos (brillo, platillos). |

- [ ] Cada perilla cambia su rango. ENV otra vez vuelve a la página normal.

### 4.9 Página de efectos del break (FX tocado)

Tocá **FX** (sin mantener) en la página AMENITY:

| Perilla | Efecto |
|---|---|
| **K1** | Saturación: ensucia y calienta, como un amplificador al máximo. |
| **K2** | Compresión: más apretado y parejo, más "pegado". |
| **K3** | LO-FI: como un sampler viejo, crujiente y opaco. |

- [ ] Las tres hacen algo audible. FX otra vez vuelve.

> El orden de los efectos del break es: break → lo-fi → feedback → ecualizador → saturación → compresión →
> filtro. Los efectos compensan el volumen: subirlos cambia el carácter, no cuánto suena.

---

## 5. Los mutes (en las dos páginas)

| Botón | Calla o devuelve |
|---|---|
| **HOME** | El break. |
| **SAVE** | El bajo. |
| **ARP** | La armonía (acordes). |
| **SEQ** | La melodía. |

- **Tocado:** el cambio es inmediato.
- **Con SEL mantenido:** el cambio espera al final de la frase. Así suena prolijo, como lo haría un DJ.

- [ ] Cada botón calla y devuelve su pista.
- [ ] Con SEL, el cambio espera unos segundos y entra justo al empezar una frase.

---

## 6. Página FACILITY: la banda

Para llegar: **GLO** hasta que diga FACILITY en rojo. Las teclas blancas y negras siguen haciendo lo mismo que
en AMENITY, así que podés romper el break mientras la banda toca.

> En esta página **SELECT no cambia el tempo**: elige la pista. Para el tempo, volvé a AMENITY con GLO.

### 6.1 El foco: qué estás controlando

**SELECT** recorre cinco lugares, y las perillas cambian según cuál esté elegido:

**BASE** (el tema entero) → **BREAK** → **BASS** (bajo) → **HARMONY** (acordes) → **MELODY** (melodía).

La línea de abajo de la pantalla dice qué hace cada control en ese foco.

- [ ] SELECT recorre los cinco y la pantalla muestra cuál está elegido.

### 6.2 BASE: el tema entero

| Control | Qué hace | Probá |
|---|---|---|
| **ALGO** | **Estilo**: cambia toda la banda (cómo toca cada uno, sonidos, rengueo y efectos). | [ ] Los 5 estilos suenan distintos. |
| **PRESET** | **Recorrido de acordes**: el camino que hace el fondo. AUTO, o uno de los 4 del estilo. | [ ] Cambia el clima del fondo. |
| **K1 ENERGY** | Cuánto toca la banda: baja, con espacio y tranquila; alta, llena y con más notas. | [ ] |
| **K2 CHORDS** | Qué tan seguido cambia el fondo: SLOW, AS IS (normal) o FAST. | [ ] |
| **K3 AUTO DUB** | De vez en cuando una pista desaparece un momento y vuelve, como en el dub. OFF: nunca. | [ ] Se notan "huecos" que vuelven solos. |
| **K4 FORM** | OFF: la banda da vueltas sin fin. ON: arma un tema con partes (intro, estrofa, estribillo, puente...). +BRK: igual, y además saca y mete el break, con rellenos en los cambios. | [ ] Con ON, arriba a la derecha aparecen la parte y su avance, como `CHORUS 3/8`. |

**Los cinco estilos, en criollo:**

| Estilo | Cómo se siente | Velocidad típica |
|---|---|---|
| **BREAKCORE** | Caótico, rápido y ruidoso; electrónica extrema. | ~170 |
| **JAZZ** | Suelto, con rengueo, bajo que camina, acordes de piano. | ~100 |
| **ACID** | El bajo chirriante y nervioso del acid house, hipnótico. | ~135 |
| **HIP HOP** | Pesado, con balanceo, piano eléctrico de fondo. | ~125 |
| **DUB** | Lento, mucho espacio, ecos y reverb, bajo profundo, guitarra a contratiempo. | ~85 |

> El tempo no cambia solo con el estilo: lo ponés en AMENITY. Si un estilo suena demasiado rápido o lento,
> acercalo a su velocidad típica. Igual, la banda se adapta tocando a la mitad o al doble cuando hace falta
> (la línea de abajo dice `HALF SPEED` o `DOUBLE SPEED`).

### 6.3 BREAK (dentro de Facility)

| Control | Qué hace |
|---|---|
| **PRESET** | Cambia de break. |
| **K1 LEVEL** | Volumen del break dentro de la banda. |
| **K2 / K3 / K4** | VARIATION, WARP y FILTER, como en AMENITY. |

- [ ] El volumen del break cambia sin afectar al resto.

### 6.4 BASS, HARMONY y MELODY

Las tres pistas se manejan igual:

| Control | Qué hace |
|---|---|
| **ALGO** | **Modo**: la manera de tocar (tablas de abajo). AUTO: lo elige el estilo. |
| **PRESET** | **Sonido**: el instrumento. AUTO, después los sonidos propios y después las voces del banco de fábrica del FM-1 (el nombre sale en pantalla). |
| **K1 LEVEL** | Volumen de la pista. |
| **K2, K3, K4** | Retoques del sonido (tablas de abajo). En el medio (0) suena como viene. |
| **SEL + ALGO** | Estilo propio de esa pista. Ejemplo: un tema DUB con melodía de JAZZ. `= BASE` sigue al tema. |

**BASS (bajo)**

| Modo | Cómo toca |
|---|---|
| KICK | Junto con el bombo, golpe a golpe. |
| LONG | Notas largas que sostienen. |
| RAGGA | Saltarín, al estilo ragga jungle. |
| ACID | Línea nerviosa y chirriante, con notas que se deslizan. |
| WALK | "Camina" nota por nota, como un contrabajo de jazz. |
| 1 DROP | Deja vacío el primer golpe, típico del reggae y el dub. |
| RIFF | Una figura corta que se repite. |

| Perilla | Qué cambia |
|---|---|
| K2 CUTOFF | Más oscuro ↔ más brillante. |
| K3 RESO | Agrega un silbido o chirrido (el del acid). |
| K4 SLIDE | Las notas se deslizan de una a otra. |

Sonidos propios: SUB, ACID, REESE, FINGER, FM BASS, DUB. Después, 16 voces del banco.

**HARMONY (acordes)**

| Modo | Cómo toca |
|---|---|
| SKANK | Golpes cortos a contratiempo, el "chuk" del reggae. |
| JAZZ | Acordes sueltos que van y vienen, como un pianista de jazz. |
| STABS | Golpes cortos y fuertes. |
| PAD | Un colchón de sonido sostenido. |
| MIX | Mezcla de golpes. |
| MIX+PAD | Golpes con colchón. |

| Perilla | Qué cambia |
|---|---|
| K2 BRIGHT | Más opaco ↔ más brillante. |
| K3 MOVEMENT | Cuánto se mueve el sonido mientras suena. |
| K4 DECAY | Cuánto dura cada golpe. |

Sonidos propios: ORGAN, SAW, EPIANO, CLAVI (clavinet), HARPSI (clavecín), GUITAR, BRASS (vientos). Después,
56 voces del banco.

**MELODY (melodía)**

| Modo | Cómo toca |
|---|---|
| SEED | Toma una melodía conocida (la "semilla") y la va transformando. |
| RIFF | Un motivo corto que se repite. |
| LINE | Una línea larga que no para. |
| ANSWER | Pregunta y respuesta, como una conversación. |

| Perilla | Qué cambia |
|---|---|
| K2 BRIGHT | Más opaco ↔ más brillante. |
| K3 GLIDE | Desliza entre notas. |
| K4 VIBRATO | Hace temblar la nota. |

**SEL + PRESET** elige la **semilla**: melodías clásicas conocidas, adaptadas al ritmo del break. Son DIES IRAE,
TOCCATA, FIFTH (la Quinta de Beethoven), MOUNTAIN KING, FUR ELISE, MINUET, GREENSLEEVES, GYMNOPEDIE, BADINERIE y
ENTERTAINER. Si reconocés una, ¡es esa! No suena idéntica porque se va transformando.

Sonidos propios: SAW, SQUARE, FM BELL (campana), REED (lengüeta), STRING, FLUTE, VIBES, PLUCK (pulsado), BRASS.
Después, 38 voces del banco.

- [ ] En cada pista probé todos los modos con ALGO.
- [ ] En cada pista recorrí los sonidos con PRESET, incluidas varias voces del banco.
- [ ] K2, K3 y K4 hacen algo audible en cada pista.
- [ ] SEL + ALGO le da otro estilo a una pista.
- [ ] SEL + PRESET en MELODY cambia la semilla.
- [ ] Ninguna combinación desafina ni se corta.

### 6.5 Rengueo y "toque humano" (SEL mantenido)

Mantené **SEL**: K1 y K2 cambian de función (lo dice la pantalla).

| Foco | SEL + K1 | SEL + K2 |
|---|---|---|
| **BASE** | **SWING**: el rengueo de toda la banda. OFF/50 es derecho y 75 muy balanceado. | **HUMAN**: pequeñas imperfecciones, como si tocaran personas. 0 es una máquina. |
| **BASS, HARMONY** | Rengueo propio de la pista (`=` sigue al tema). | **ACCENTS**: diferencia entre golpes fuertes y suaves. |
| **MELODY** | Rengueo propio. | **EXPRESSION**: cuánto "respira" la melodía (sube y baja de intensidad). |
| **BREAK** | Rengueo del break (arranca en OFF). | — |

`A:` delante de un valor significa que lo eligió el estilo.

- [ ] Con SWING alto, el ritmo se balancea; en OFF, va derecho.
- [ ] Con HUMAN en 0 suena exacto; alto, más suelto, pero nunca "mal tocado". Si suena desprolijo, reportalo.

### 6.6 Efectos de la banda (FX en la página FACILITY)

En FACILITY, tocá **FX**: se prende su luz y las perillas manejan los efectos de la banda. FX otra vez vuelve.
**SEL + perilla** devuelve ese valor a AUTO.

| Foco | Controles |
|---|---|
| **BASE** | **ALGO**: tipo de reverb (ROOM = sala, SPRING = resorte, metálico y vintage). **PRESET**: tiempo del eco (a ritmo, o libre en milisegundos). **K1 FEEDBACK**: cuántas veces se repite el eco. **K2 COLOR**: eco más oscuro o más claro. **K3 DECAY**: cuánto dura la reverb. **K4 RETURN**: volumen general de los efectos. |
| **BREAK** | **K1**: cuánto break va al eco. **K2**: cuánto va a la reverb. **K3 THROWS**: la última caja de cada frase sale disparada al eco (truco del dub). |
| **BASS, HARMONY, MELODY** | **K1**: cuánto de la pista va al eco. **K2**: cuánto va a la reverb. **K3 CHORUS**: ensancha y hace "ondular" el sonido. **K4 PAN**: izquierda ↔ derecha. |

- [ ] Eco y reverb se escuchan en cada pista.
- [ ] Con FEEDBACK muy alto (más de 100), el eco se realimenta solo y crece. Al apretar STOP se apaga solo.
      Bajá el volumen antes de probarlo.
- [ ] PAN mueve la pista de un lado al otro (con auriculares).
- [ ] Con el break solo y los envíos del break en 0, el break suena igual que sin Facility.

---

## 7. Guardar y cargar escenas (slots)

Hay **16 lugares** (uno por tecla blanca) para guardar todo lo que armaste: break, tempo, estilo, pistas,
sonidos, mutes, rengueo y efectos.

| Control | Qué hace |
|---|---|
| **ENV + EDIT + blanca** (mantener ENV y EDIT, tocar la tecla) | **Guarda** en esa tecla. Si está tocando, dice `SAVED AT STOP` y lo escribe cuando parás. |
| **ENV + blanca** | **Carga**. Tocando, entra en el próximo bloque de 4 compases, para no cortar. Parado, al instante. |
| **ENV + SEL + blanca** | **Carga con un tema nuevo**: la misma configuración, pero la banda inventa otras notas. |

Al encender, todo arranca de cero (160 BPM, AMEN). Lo que quieras conservar, guardalo en un slot.

- [ ] Guardé con el aparato parado y lo cargué después.
- [ ] Guardé tocando: dijo SAVED AT STOP y, después de parar, el slot quedó guardado.
- [ ] Cargar tocando entra a tiempo, sin cortes.
- [ ] Cargar con SEL da un tema distinto con la misma configuración.
- [ ] Apagué, encendí y los slots seguían ahí.
- [ ] Un slot vacío dice EMPTY SLOT.

> Al guardar parado puede haber un silencio cortito: es la escritura en la memoria. Es normal.

---

## 8. Tocar con otros aparatos (MIDI clock)

El FM-1 se puede sincronizar con una compu (Ableton, por ejemplo), una caja de ritmos u otro sinte, por USB,
Bluetooth o el conector TRS.

- **Seguir a otro:** cuando el otro aparato da *Start*, Amenity+ arranca a su tempo. La pantalla dice
  **EXT**, y mientras sigue, PLAY y el tempo no se usan. *Stop* (o medio segundo sin señal) lo para.
- **Ser el que manda:** tocando solo, el FM-1 manda su tempo por USB y Bluetooth, y los demás lo pueden
  seguir.

- [ ] Siguiendo a Ableton u otro aparato: arranca, para y sigue cambios de tempo.
- [ ] Facility también sigue ese tempo.
- [ ] Mandando: el otro aparato sigue al FM-1.

---

## 9. Tus propios breaks

Podés cargar hasta **4 breaks propios** desde **https://marotito.github.io/amenity/breaks/** (Chrome o Edge,
FM-1 conectado por USB, con Amenity+ parado). Arrastrá un archivo WAV de 1, 2, 4 u 8 compases de batería a uno
de los 4 lugares. La página ajusta todo sola: volumen, formato y qué golpes van en las teclas negras. Después
aparecen en PRESET, después de los de fábrica. En la misma página se cambian de orden y se borran.

Instalar otro firmware, o volver al de fábrica, borra los breaks propios.

- [ ] Cargué un break propio y suena en PRESET.
- [ ] Sus golpes aparecen en las negras 1–8.
- [ ] Cambié el orden y lo borré desde la página.

---

## 10. Qué tan exigido está el aparato (CPU)

El FM-1 tiene un procesador chico. Estas herramientas sirven para ver si se queda corto.

**EDIT mantenido** muestra arriba:

- **`CPU nn%`**: cuánto trabaja. Por debajo de 80 está tranquilo.
- **`Bnn Tnn Fnn`**: cuánto se lleva el **B**reak, las pis**T**as de la banda y los e**F**ectos.
- **`FM nn/nn !n`**: lo que piden los sonidos del banco y cuánto se les permite, y **`!`**: cuántas veces
  estuvo cerca del límite desde que lo encendiste.

**EDIT + GLO** activa el **test de CPU**: pone todo al máximo a propósito (todas las voces sonando con el
sonido más caro y todos los efectos). No suena musical: es una prueba de esfuerzo. EDIT + GLO otra vez lo
apaga.

**Qué pasa cuando no da más.** El aparato se cuida solo: si se acerca al límite, deja de sonar alguna nota del
colchón de acordes (nunca el bajo ni la melodía), con un fundido muy corto para que no haga "clic".

**`AUDIO OVERLOAD` en pantalla significa que el sonido se cortó. No debería pasar nunca: si lo ves, es lo más
importante para reportar.**

- [ ] Toqué 10 minutos o más cambiando estilos, modos y sonidos (también del banco): nunca apareció
      AUDIO OVERLOAD.
- [ ] Test de CPU con el break a fondo (blanca 3 mantenida o K3 WARP alto, y K1 FEEDBACK en 100): anoté CPU,
      `B T F` y `FM nn/nn !n` después de 2 minutos.

---

## 11. Qué es normal (no hace falta reportarlo)

- `!` sube de a 2 cada vez que arranca un tema: es un esfuerzo de un instante al inventar la canción, y no
  corta el sonido. Se va a corregir.
- Con sonidos pesados del banco en todas las pistas, el colchón de acordes puede perder alguna nota: es el
  aparato cuidándose.
- Un silencio cortito al guardar un slot con el aparato parado.
- Un "pop" al encender: viene de la base del firmware.
- La melodía a veces hace cosas raras al final de una frase: está en revisión, así que igual contanos cuál y
  dónde.

---

## 12. Recorrido completo (lista de control)

Si querés probar todo de una, este es el orden recomendado. Cada punto remite a su sección.

1. [ ] Instalación y versión (§1)
2. [ ] Primeros cinco minutos (§3)
3. [ ] Tempo, OCT, break y tono (§4.1, §4.2)
4. [ ] 16 teclas blancas (§4.3)
5. [ ] Teclas negras (§4.4)
6. [ ] K1–K4 de AMENITY (§4.5)
7. [ ] FX + tecla: zumbido y tonalidad (§4.6)
8. [ ] REC, LFO, EDIT + HOME (§4.7)
9. [ ] Ecualizador y efectos del break (§4.8, §4.9)
10. [ ] Mutes (§5)
11. [ ] Foco, BASE y los 5 estilos (§6.1, §6.2)
12. [ ] BREAK en Facility (§6.3)
13. [ ] Bajo, acordes y melodía: modos, sonidos, perillas y semillas (§6.4)
14. [ ] Rengueo y toque humano (§6.5)
15. [ ] Efectos de la banda (§6.6)
16. [ ] Slots (§7)
17. [ ] MIDI clock (§8), si tenés con qué
18. [ ] Breaks propios (§9)
19. [ ] Estabilidad y test de CPU (§10)

---

## 13. Cómo reportar

Abrí un issue en **https://github.com/Marotito/amenity/issues** con la plantilla *Reporte de error*, o mandá
lo mismo por mensaje.

1. **Versión** (`2.0.1-devNN`).
2. **Qué estabas haciendo:** página, foco, estilo, y modo y sonido de cada pista (lo que diga la pantalla
   alcanza). Si estaba sincronizado con otro aparato, cuál.
3. **Qué pasó** y **qué esperabas**, en tus palabras. No hace falta vocabulario técnico.
4. **Si se repite:** los pasos para que vuelva a pasar.
5. **CPU:** con EDIT mantenido, los números de arriba.
6. **Si es de sonido:** una grabación corta (el FM-1 manda audio por USB a la compu) o un video con el
   celular. Una foto de la pantalla también ayuda.

También valen las opiniones musicales: "en DUB la melodía termina raro", "este sonido del banco queda mejor en
HIP HOP", "JAZZ suena demasiado lleno". Decí estilo, pista y sonido.
