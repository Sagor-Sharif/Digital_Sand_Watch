GitHub Repository Description (Long + Professional)
🕒 Digital Sand Hourglass — Arduino + MPU6050 + Dual I2C RGB Matrices

This project simulates a real sand hourglass using:

Arduino (Pro Mini / Uno / Nano)

Two 8×8 RGB LED I2C matrices (Colorduino-style)

MPU6050 for gravity sensing

Potentiometer for duration selection

Realistic falling sand physics

Unlike simple LED countdown timers, this project recreates true granular movement:

 Key Features

Realistic Sand Physics

Sand falls diagonally through the “neck”

Grains spread left/right depending on tilt

Dynamic pyramid formation at the bottom

A moving “hole” animation makes top sand look like it falls naturally into 8H

Gravity Sensitive (MPU6050)

Tilt right → sand slides right

Tilt left → sand slides left

Flip upside-down → the hourglass restarts automatically

Based on live X/Y/Z angle analysis

Dual 8×8 LED Display Simulation

Top display drains sand logically from 1A → 8H

Bottom display fills realistically from 8H → 1A

Full orientation correction using rotation matrices

Real Hourglass Behavior

Sand always funnels through 8H

When top sand drains, the top display shows a “hole” climbing upward (refilling effect)

Bottom display builds a perfect pyramid shape

Configurable Timer

30 sec, 1 min, 2 min, 5 min, 10 min, 30 min (via potentiometer)

No External Libraries Needed (except MPU6050_tockn)

 Physics Engine Breakdown

✔ Downward movement
✔ Diagonal slide
✔ Left/Right alternate spread
✔ Column height detection
✔ Tilt-based slope forces
✔ Top hole back-flow movement
