# Amenity+ · Historial

Versiones de prueba `2.0.1-devN` de la rama `amenity-plus`. Las que pasaron por el aparato están marcadas con ✔.

## dev25 ✔ (estable, 19 nov 2026)
- **Test de CPU honesto:** las 8 voces con SYN PAD 1 y el brillo arriba, con los 6 operadores sonando (el test
  anterior era ~1,5 veces más liviano que el peor caso real).
- **Sin denormales:** las cuerdas pulsadas, los filtros sin entrada, las envolventes y el echo cortan a 0 los
  valores casi nulos, que en algunas FPU son muy lentos.
- **Gobernador nuevo** (como el de fm1_open): cada voz FM tiene un costo, hay un presupuesto que aprende y la
  armonía cede antes de que se corte el audio. El bajo y la melodía no se cortan nunca.
- Pantalla: `FM costo/presupuesto !n`.
- Medido: test de CPU 74 % de pico, sin cortes; tocando acid y dub, sin cortes.

## dev24 ✔ (19 nov 2026)
- El render va pista por pista entre eventos; es bit a bit igual al anterior.
- El núcleo FM6 y la mezcla con los efectos corren desde RAM.
- Primer gobernador de voces y medidor de CPU por partes (EDIT).
- Medido con el test anterior: 73–75 %, pico 76 % (antes 80–83 %, pico 85 %). Tocando con voces del banco
  aparecía `AUDIO OVERLOAD`: lo corrige dev25.
- (dev23 falló en el CI y no se publicó.)

## dev22 (19 nov 2026) · F7a, ritmo contra el break
- Las semillas clásicas en 3/4 y 3/8 se reescriben en 4/4.
- Las notas largas no caen en la caja, la armonía deja lugar a los ataques de la melodía y el jazz no hace
  comping en hemiola bajo la melodía.

## dev21 ✔ / dev20 (19 nov 2026) · F6d, FM6
- Motor DX7 (msfa/Dexed, port de Felucca) y el banco de fábrica del FM-1 en bajo, armonía y melodía.

## dev18–dev19 (19 nov 2026) · F6b–F6c, sonidos y armonía
- Cuerda pulsada (CLAVI, HARPSI, GUITAR, PLUCK), EPIANO, VIBES, FM BASS, DUB, FLUTE, BRASS, REED.
- La melodía escucha la armonía, y la aproximación del bajo va por grados de la escala. FINGER reemplaza a
  UPRIGHT.

## dev17 ✔ (18 nov 2026) · F6a, efectos
- Echo de cinta, reverb ROOM y SPRING, chorus, pan, throws y vista FX de Facility.

## dev16 ✔ / dev15 (18 nov 2026) · F5–F5b, groove y expresión
- Swing y humanize por estilo y por pista, acentos y strum.
- Expresión de la melodía con reglas de interpretación y sin timing aleatorio.

## dev3 ✔ y anteriores · F1–F4
- Esqueleto de Facility: estilos, forma de tema, slots y melodía (prototipo).
