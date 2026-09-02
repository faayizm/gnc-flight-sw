#!/usr/bin/env python3
"""
Orbits, from "why doesn't it fall down" to actually integrating one.

Run me:  python3 learn/toolbox/orbit_sandbox.py

Pure Python -- no numpy, no dependencies. Used by lesson 12-orbits.
"""

import math

MU_EARTH = 3.986004418e14      # m^3/s^2, Earth's gravitational parameter
R_EARTH = 6_371_000.0          # m, mean radius

BOX = "─" * 66


def title(text: str) -> None:
    print(f"\n{BOX}\n  {text}\n{BOX}")


def circular_speed(altitude_m: float) -> float:
    """How fast you must go, sideways, to stay in a circle at this height."""
    return math.sqrt(MU_EARTH / (R_EARTH + altitude_m))


def orbital_period(altitude_m: float) -> float:
    """Seconds to go all the way round."""
    r = R_EARTH + altitude_m
    return 2 * math.pi * math.sqrt(r ** 3 / MU_EARTH)


def escape_speed(altitude_m: float) -> float:
    return math.sqrt(2 * MU_EARTH / (R_EARTH + altitude_m))


def main() -> None:
    print("""
                  WHY DOESN'T A SATELLITE FALL DOWN?

  It does. Constantly. It is falling right now.

  The trick is that it is also moving sideways so fast that by the
  time it has fallen, the ground has curved away underneath it. It
  keeps falling and keeps missing. That is an orbit: falling, and
  missing the planet.

  Newton drew this in 1687. Fire a cannonball horizontally from a
  very tall mountain:

        slow  ──▶  ....                  lands nearby
                 ▁▁▁▁▁▁▁▁
        faster ──▶ .......                lands further away
                 ▁▁▁▁▁▁▁▁▁▁▁▁▁▁
        fast   ──▶ ..............         goes right around
                 ▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁

  Nothing about being "in space" makes you weightless. Gravity at the
  Space Station is about 90% of what it is on the ground. Astronauts
  float because they are FALLING, not because gravity has gone away.
""")

    title("How fast is fast enough?")

    print(f"\n  {'WHERE':<26}{'ALTITUDE':>10}{'SPEED':>12}{'ONE ORBIT':>14}")
    print("  " + "-" * 62)

    places = [
        ("Sea level (impossible)", 0),
        ("A very tall mountain", 9_000),
        ("Space Station", 400_000),
        ("This project's satellite", 550_000),
        ("Sun-synchronous imaging", 800_000),
        ("GPS", 20_200_000),
        ("Geostationary (TV)", 35_786_000),
    ]

    for name, altitude in places:
        speed = circular_speed(altitude)
        period = orbital_period(altitude)
        if period < 7200:
            period_text = f"{period / 60:.0f} min"
        else:
            period_text = f"{period / 3600:.1f} hours"
        print(f"  {name:<26}{altitude / 1000:>8.0f}km{speed / 1000:>10.2f}km/s"
              f"{period_text:>14}")

    print(f"""
  Notice: the HIGHER you go, the SLOWER you travel, and the LONGER
  one lap takes. That feels backwards until you realise gravity is
  weaker up there, so less speed is needed to balance it.

  Geostationary is the special one. At {35786:,} km an orbit takes
  exactly one day, so the satellite sits above the same spot on
  Earth forever. That is why a TV dish never has to move.
""")

    title("What if you get the speed wrong?")

    altitude = 550_000
    correct = circular_speed(altitude)
    r = R_EARTH + altitude

    print(f"\n  Target: a circle at {altitude/1000:.0f} km. Correct speed: "
          f"{correct/1000:.3f} km/s\n")
    print(f"  {'YOUR SPEED':>14}  {'RESULT':<44}")
    print("  " + "-" * 62)

    for factor in (0.85, 0.95, 1.00, 1.05, 1.20, 1.4143):
        v = correct * factor
        energy = v * v / 2 - MU_EARTH / r
        if energy >= 0:
            outcome = "escapes Earth entirely -- never comes back"
        else:
            a = -MU_EARTH / (2 * energy)            # semi-major axis
            # Specific angular momentum for a horizontal burn is just r*v.
            h = r * v
            e = math.sqrt(max(0.0, 1 + 2 * energy * h * h / (MU_EARTH ** 2)))
            perigee = a * (1 - e) - R_EARTH
            apogee = a * (1 + e) - R_EARTH
            if perigee < 0:
                outcome = "falls back down -- hits the ground"
            elif perigee < 100_000:
                outcome = f"dips to {perigee/1000:.0f} km -- burns up in the air"
            elif abs(factor - 1.0) < 1e-9:
                outcome = "a perfect circle"
            else:
                outcome = f"an ellipse: {perigee/1000:.0f} km up to {apogee/1000:.0f} km"
        print(f"  {v/1000:>10.3f} km/s  {outcome:<44}")

    print(f"""
  Too slow and you come back down. Too fast and you swing out into
  an ellipse. About 41% too fast ({escape_speed(altitude)/1000:.2f} km/s) and you leave
  for good -- that is escape velocity.

  This is why launch is so unforgiving. Being 5% off is not "close".
""")

    title("Let's actually fly one")

    print("""
  Now we integrate the real equation of motion, step by step:

      acceleration = -mu * position / |position|^3

  That is Newton's law of gravitation, and it is the entire physics
  of an orbit. Everything else is detail.
""")

    def simulate(speed_factor: float, steps: int = 60_000, dt: float = 1.0):
        """Velocity-Verlet integration of a two-body orbit in 2D."""
        x, y = r, 0.0
        vx, vy = 0.0, circular_speed(altitude) * speed_factor

        def accel(px: float, py: float) -> tuple[float, float]:
            d = math.sqrt(px * px + py * py)
            k = -MU_EARTH / (d ** 3)
            return k * px, k * py

        ax, ay = accel(x, y)
        lowest = highest = math.hypot(x, y)

        for _ in range(steps):
            x += vx * dt + 0.5 * ax * dt * dt
            y += vy * dt + 0.5 * ay * dt * dt
            nax, nay = accel(x, y)
            vx += 0.5 * (ax + nax) * dt
            vy += 0.5 * (ay + nay) * dt
            ax, ay = nax, nay

            d = math.hypot(x, y)
            lowest = min(lowest, d)
            highest = max(highest, d)
            if d < R_EARTH:
                return None, None          # hit the ground
        return lowest - R_EARTH, highest - R_EARTH

    print(f"  {'SPEED':>8}  {'LOWEST POINT':>14}  {'HIGHEST POINT':>15}")
    print("  " + "-" * 62)
    for factor in (0.90, 1.00, 1.10):
        low, high = simulate(factor)
        if low is None:
            print(f"  {factor*100:>6.0f}%  {'CRASHED':>14}")
        else:
            print(f"  {factor*100:>6.0f}%  {low/1000:>12.0f} km  {high/1000:>13.0f} km")

    print("""
  Those numbers came from nothing but Newton's law and arithmetic,
  repeated sixty thousand times. No orbital mechanics formulas, no
  library. That is what a simulator does -- and it is what sim/ in
  this repository will do in Phase 2, with more forces added.
""")

    title("Why our satellite needs to know all this")

    print("""
  To point a camera at a spot on Earth, the spacecraft must know
  where it IS. To talk to a ground station, it must know when it
  will be overhead. To manage its battery, it must know when it
  enters Earth's shadow.

  So the flight software carries its own orbit model on board and
  propagates it forward between GPS fixes. In ADCS_HK you will find
  three fields -- pos_eci_x, pos_eci_y, pos_eci_z -- which are the
  spacecraft's own answer to "where am I?"

  In Phase 2 those fields stop being zero.

  TRY THIS
  1. Change `altitude` and rerun. How slow can an orbit get?
  2. Set dt = 60.0 in simulate(). The answers get worse. Why?
     (This is the single most important question in simulation.)
  3. Real orbits also feel Earth's bulge and the thin outer air.
     Which of the numbers above would those change most?
""")


if __name__ == "__main__":
    main()
