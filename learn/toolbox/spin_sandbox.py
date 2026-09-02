#!/usr/bin/env python3
"""
Why a tumbling satellite keeps tumbling, and how a magnet stops it.

Run me:  python3 learn/toolbox/spin_sandbox.py

Pure Python. Used by lessons 13-attitude and 16-control.
"""

import math

BOX = "─" * 66


def title(text: str) -> None:
    print(f"\n{BOX}\n  {text}\n{BOX}")


# -- tiny vector helpers, so the physics below reads like the equations ------

def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a):
    return math.sqrt(dot(a, a))


def scale(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


# -- the spacecraft ----------------------------------------------------------

# Moment of inertia about each body axis, in kg m^2. A 6U CubeSat, roughly.
# "Moment of inertia" is to rotation what mass is to straight-line motion:
# how hard it is to change how fast something is spinning.
INERTIA = (0.05, 0.06, 0.04)

MAX_DIPOLE = 0.2          # A m^2, what a small magnetorquer can produce
BDOT_GAIN = 60000.0       # tuning knob for the control law


def sparkline(values, width=58, height=9):
    """A tiny ASCII plot, so you can SEE the rate come down."""
    if not values:
        return ""
    step = max(1, len(values) // width)
    sampled = values[::step][:width]
    top = max(sampled) or 1.0
    rows = []
    for level in range(height, 0, -1):
        threshold = top * level / height
        row = "".join("█" if v >= threshold else " " for v in sampled)
        rows.append(f"  {threshold:6.2f} |{row}")
    rows.append("         +" + "-" * len(sampled))
    return "\n".join(rows)


def main() -> None:
    print("""
             WHY IS A TUMBLING SATELLITE SUCH A PROBLEM?

  A satellite is thrown off its rocket spinning. Nobody means for
  that to happen; it just does. And here is the trouble:

      IN SPACE, NOTHING SLOWS IT DOWN.

  On Earth a spinning top stops because of friction with the air and
  the table. In orbit there is no table and almost no air. A spin
  started at deployment lasts for YEARS unless something stops it.

  That matters because a tumbling satellite:
      * cannot point its solar panels at the Sun    -> battery dies
      * cannot point its antenna at the ground      -> no contact
      * cannot point its camera at anything         -> useless

  So the very first thing flight software does after separation is
  stop the tumble. It is called DETUMBLING, and it must work with no
  help from the ground, because until it works there is no ground
  contact.
""")

    title("Rotation is stranger than you expect")

    print("""
  Push a box in space and it goes in a straight line forever. Simple.

  Spin a box in space and it does NOT simply keep spinning about the
  same axis. Unless you spin it exactly about one of its three
  principal axes, the axis itself wanders. This is Euler's equation:

      I * dw/dt  =  torque  -  w x (I * w)
                                └──────┬──────┘
                             this term exists even with
                             ZERO torque, and it is why
                             rotation is not intuitive

  You have seen this: a tennis racket flipped about its middle axis
  always twists. A spinning phone tossed in the air tumbles oddly.
  It is called the intermediate axis theorem, and it is real physics
  our satellite has to live with.
""")

    title("The trick: push against Earth's magnetic field")

    print("""
  A satellite has nothing to push against. No air, no ground.

  But Earth is a giant magnet, and its field reaches into orbit. So
  put electromagnets -- MAGNETORQUERS -- on board. Run current
  through a coil and it becomes a magnet. A magnet in a magnetic
  field feels a twisting force:

      torque = m x B          m = our coil's magnetic moment
                              B = Earth's field where we are

  No fuel. No moving parts. Just electricity and the planet. A coil
  the size of a pencil can detumble a small satellite in an hour.

  The catch, and it is a real one: a cross product is always
  PERPENDICULAR to both inputs. So you can never make torque along
  the direction of B. You get to control two axes, not three, at any
  instant. It works out because B keeps changing direction as the
  satellite goes round its orbit -- but it means detumbling takes an
  orbit or so, not a minute.
""")

    title("The B-dot control law")

    print("""
  Now the clever bit, and it is beautifully simple.

  You do not need to know your attitude. You do not need a star
  tracker, a Sun sensor, or a working orbit model. You only need to
  notice that if you are spinning, the measured magnetic field
  SWEEPS AROUND in your body frame.

  So: measure how fast B appears to change, and push against it.

      m  =  -k * dB/dt

  That is the whole control law. One line. "B-dot" is engineering
  shorthand for "the time derivative of B".

  It is the first thing that runs on a new satellite, precisely
  because it needs almost nothing to work.
""")

    title("Let's run it")

    # Start tumbling badly, in a plausible way after separation.
    omega = (0.14, -0.10, 0.07)              # rad/s, roughly 10 deg/s total
    dt = 0.1
    duration = 5400.0                         # one orbit
    steps = int(duration / dt)

    b_magnitude = 3.0e-5                      # tesla, typical in low orbit
    orbit_rate = 2 * math.pi / 5400.0         # rad/s, one orbit in 90 minutes

    # The spacecraft's ORIENTATION, as a rotation matrix taking a vector from
    # inertial coordinates into body coordinates. We have to track this
    # honestly: the whole point is that the magnetometer sees the field in
    # BODY axes, and that is what makes it appear to sweep when we spin.
    rotation = [(1.0, 0.0, 0.0),
                (0.0, 1.0, 0.0),
                (0.0, 0.0, 1.0)]

    def apply(matrix, vector):
        return tuple(dot(row, vector) for row in matrix)

    def rotation_rate(matrix, w):
        """
        R_dot = -[w]x R, written out row by row.

        Note this is NOT the same as taking the cross product of w with each
        row -- the skew-symmetric matrix mixes the rows together. Getting this
        wrong makes a satellite spin UP instead of down, which is exactly the
        bug that was in this file before it was written out properly.
        """
        r0, r1, r2 = matrix
        return [
            add(scale(r1, w[2]), scale(r2, -w[1])),
            add(scale(r0, -w[2]), scale(r2, w[0])),
            add(scale(r0, w[1]), scale(r1, -w[0])),
        ]

    def orthonormalise(matrix):
        """
        Numerical integration slowly destroys the 'rotation-ness' of a matrix
        -- rows stop being unit length and stop being perpendicular. Gram-
        Schmidt puts it back. Every attitude system needs an equivalent step,
        which is one reason quaternions are popular: they need only a single
        divide to renormalise.
        """
        r0 = scale(matrix[0], 1.0 / norm(matrix[0]))
        r1 = add(matrix[1], scale(r0, -dot(matrix[1], r0)))
        r1 = scale(r1, 1.0 / norm(r1))
        r2 = cross(r0, r1)
        return [r0, r1, r2]

    history = []
    prev_b_body = None

    for step in range(steps):
        t = step * dt

        # Earth's field along the orbit, in inertial coordinates. A real
        # mission uses the IGRF model; this is a stand-in with the right
        # character -- roughly the right size, and sweeping once per orbit.
        angle = orbit_rate * t
        b_inertial = (b_magnitude * math.cos(angle),
                      b_magnitude * math.sin(angle),
                      b_magnitude * 0.4 * math.sin(2 * angle))

        # What the magnetometer actually reads: the field in BODY axes.
        b_body = apply(rotation, b_inertial)

        if prev_b_body is None:
            b_dot = (0.0, 0.0, 0.0)
        else:
            b_dot = scale((b_body[0] - prev_b_body[0],
                           b_body[1] - prev_b_body[1],
                           b_body[2] - prev_b_body[2]), 1.0 / dt)
        prev_b_body = b_body

        # ---- THE CONTROL LAW ----------------------------------------------
        # Three lines. This is the entire detumble controller.
        dipole = scale(b_dot, -BDOT_GAIN)

        # Real coils saturate. Ignoring that would make the demo lie, and the
        # saturated phase is clearly visible in the plot below.
        magnitude = norm(dipole)
        if magnitude > MAX_DIPOLE:
            dipole = scale(dipole, MAX_DIPOLE / magnitude)

        torque = cross(dipole, b_body)

        # ---- EULER'S EQUATION ---------------------------------------------
        iw = (INERTIA[0] * omega[0], INERTIA[1] * omega[1], INERTIA[2] * omega[2])
        gyroscopic = cross(omega, iw)
        domega = ((torque[0] - gyroscopic[0]) / INERTIA[0],
                  (torque[1] - gyroscopic[1]) / INERTIA[1],
                  (torque[2] - gyroscopic[2]) / INERTIA[2])
        omega = add(omega, scale(domega, dt))

        # ---- ATTITUDE KINEMATICS ------------------------------------------
        # How the orientation itself changes as the spacecraft turns.
        derivative = rotation_rate(rotation, omega)
        rotation = [add(row, scale(d, dt)) for row, d in zip(rotation, derivative)]
        if step % 100 == 0:
            rotation = orthonormalise(rotation)

        if step % 20 == 0:
            history.append(math.degrees(norm(omega)))

    start = history[0]
    end = history[-1]

    print(f"""
  A 6U CubeSat, just separated, tumbling at {start:.1f} degrees per second.
  Magnetorquers only. No thrusters, no wheels, no ground contact.

  Spin rate over {duration/60:.0f} minutes (degrees per second):
""")
    print(sparkline(history))
    print(f"""
      start: {start:>6.2f} deg/s
      end:   {end:>6.2f} deg/s      ({100*(1-end/start):.0f}% removed)

  No fuel was used. The satellite pushed against the planet.
""")

    title("Why it slows down but never quite reaches zero")

    print("""
  Look at the tail of that plot. It flattens out rather than hitting
  zero, and that is not a bug -- it is the physics.

  B-dot can only remove rotation PERPENDICULAR to the magnetic field,
  and the field only sweeps around as fast as the orbit. So the last
  little bit of spin takes a long time, and a real mission accepts
  "slow enough" rather than "stopped".

  "Slow enough" is a number the ground can change: look up
  DETUMBLE_RATE_DPS in dictionary/mission.yaml. It is 2.0 deg/s.
  Below that, the spacecraft is calm enough for the next step --
  working out which way it is actually pointing, which needs
  sensors that only work when you are not spinning wildly.

  TRY THIS
  1. Set BDOT_GAIN to 6000. Ten times weaker. How long now?
  2. Set it to 600000. Faster? Look closely at the plot -- more gain
     is not always better, and finding out why is most of what
     control engineering is.
  3. Set MAX_DIPOLE to 0.02, a much smaller coil. What breaks?
  4. Set orbit_rate to 0 -- pretend the field never moves. Watch it
     fail to fully detumble, and work out which axis survives.
""")


if __name__ == "__main__":
    main()
