# math

The `math::` library wraps the C math functions (`sinf`, `powf`, `sqrtf`
and so on) and adds angle conversion and an integer scaling helper. Read
this page when a script needs anything beyond `+ - * / %`.

The host loads it with `wr_loadMathLib( w )` (or `wr_loadAllLibs( w )`);
in `wrench.json` its name is `math`. It needs no build flags and runs on
every platform, including small microcontrollers, but it links the C math
library (`libm`), which costs code space on the smallest targets.

```wrench
var angle = math::deg2rad( 30 );
println( "sin(30 degrees) = ", math::sin( angle ) );
println( "hypotenuse of 3 and 4 = ", math::sqrt( 3 * 3 + 4 * 4 ) );
println( "2 to the 10th = ", math::pow( 2, 10 ) );
```

Output:

```text
sin(30 degrees) = 0.5
hypotenuse of 3 and 4 = 5
2 to the 10th = 1024
```

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `math::sin` | `x` | float | sine, `x` in radians |
| `math::cos` | `x` | float | cosine, `x` in radians |
| `math::tan` | `x` | float | tangent, `x` in radians |
| `math::asin` | `x` | float | arc sine, in radians |
| `math::acos` | `x` | float | arc cosine, in radians |
| `math::atan` | `x` | float | arc tangent, in radians |
| `math::atan2` | `y, x` | float | angle of the point (x, y), -pi to pi |
| `math::sinh` | `x` | float | hyperbolic sine |
| `math::cosh` | `x` | float | hyperbolic cosine |
| `math::tanh` | `x` | float | hyperbolic tangent |
| `math::exp` | `x` | float | e to the power `x` |
| `math::log` | `x` | float | natural logarithm |
| `math::ln` | `x` | float | natural logarithm, same as `math::log` |
| `math::log10` | `x` | float | base 10 logarithm |
| `math::pow` | `base, exponent` | float | `base` to the power `exponent` |
| `math::sqrt` | `x` | float | square root |
| `math::ldexp` | `x, exp` | float | `x` times 2 to the power `exp` |
| `math::floor` | `x` | float | largest whole number not above `x` |
| `math::ceil` | `x` | float | smallest whole number not below `x` |
| `math::trunc` | `x` | float | `x` rounded toward zero |
| `math::abs` | `x` | float | absolute value |
| `math::fmod` | `x, y` | float | remainder of `x / y`, sign of `x` |
| `math::deg2rad` | `degrees` | float | degrees to radians |
| `math::rad2deg` | `radians` | float | radians to degrees |
| `math::iscale` | `a, b, divisor` | int | `(a * b) / divisor` without overflow |

The library registers no constants; there is no `math::pi`. Write the
value yourself (`3.14159265`) or use `math::deg2rad( 180 )`.

## Arguments and results

Every function except `math::iscale` works in single precision float, as
the C `...f` functions do:

* Int arguments are converted to float. A float carries about seven
  significant digits, so ints above 16777216 lose their lowest digits.
* The result is always a float, even when it is a whole number:
  `math::floor( 7.8 )` is the float `7`. Floats print without a trailing
  `.0`, so the difference only shows in arithmetic (`f / 2` is `3.5`).
  Convert with `(int)` when you need an int.
* A function called with the wrong number of arguments does not fail: it
  returns the float `0`. The one argument functions need exactly one
  argument, `atan2`, `pow`, `fmod` and `ldexp` exactly two.
* An argument that is not a number (a string, an array) is not parsed:
  its hash value is used as the number, which is never what you want.
  Convert text with [`str::tol`](str.md#strtol) first.

```wrench
println( math::sqrt(), " ", math::pow( 2 ), " ", math::sin( 1, 2 ) );
println( math::sqrt( "16" ), " ", math::sqrt( str::tol( "16" ) ) );
```

Output:

```text
0 0 0
20852.8 4
```

Results outside the real numbers follow C: `nan` and `inf`.

```wrench
println( math::sqrt( -1 ), " ", math::log( 0 ), " ", math::exp( 100 ) );
```

Output:

```text
nan -inf inf
```

## Trigonometry

`math::sin`, `math::cos` and `math::tan` take an angle in radians;
`math::asin`, `math::acos` and `math::atan` return one. The hyperbolic
functions `math::sinh`, `math::cosh` and `math::tanh` complete the set.

```wrench
println( math::sin( 0 ), " ", math::cos( 0 ), " ", math::tan( 1 ) );
println( math::asin( 1 ), " ", math::acos( 0 ), " ", math::atan( 1 ) );
println( math::sinh( 1 ), " ", math::cosh( 1 ), " ", math::tanh( 1 ) );
```

Output:

```text
0 1 1.55741
1.5708 1.5708 0.7854
1.1752 1.54308 0.76159
```

### math::atan2

`math::atan2( y, x )` returns the angle of the point (x, y) from the
positive x axis, from -pi to pi. Unlike `math::atan( y / x )` it knows the
quadrant and works when `x` is 0. The arguments are in C order, `y`
first.

```wrench
println( math::atan2( 1, 0 ), " ", math::atan2( 0, -1 ), " ", math::atan2( -1, -1 ) );
println( math::rad2deg( math::atan2( 1, 1 ) ) );
```

Output:

```text
1.5708 3.14159 -2.35619
45
```

### math::deg2rad, math::rad2deg

Convert an angle between degrees and radians (multiply by pi / 180 or
180 / pi).

```wrench
println( math::rad2deg( 3.14159265 ), " ", math::deg2rad( 90 ) );
```

Output:

```text
180 1.5708
```

## Powers and logarithms

* `math::exp( x )` is e to the power `x`.
* `math::log( x )` and `math::ln( x )` are the same function, the natural
  logarithm. `math::log10( x )` is the base 10 logarithm.
* `math::pow( base, exponent )` raises `base` to `exponent`; both may be
  fractional or negative.
* `math::sqrt( x )` is the square root.
* `math::ldexp( x, exp )` is `x * 2^exp`; `exp` is converted to an int.

```wrench
println( math::exp( 1 ), " ", math::log( 10 ), " ", math::ln( 10 ), " ", math::log10( 1000 ) );
println( math::pow( 2, 0.5 ), " ", math::pow( 10, -3 ), " ", math::sqrt( 2 ) );
println( math::ldexp( 3, 4 ), " ", math::ldexp( 1.5, -1 ) );
```

Output:

```text
2.71828 2.30259 2.30259 3
1.41421 0.001 1.41421
48 0.75
```

## Rounding and remainders

* `math::floor( x )` rounds down, `math::ceil( x )` rounds up,
  `math::trunc( x )` rounds toward zero.
* `math::abs( x )` is the absolute value, as a float even for an int.
* `math::fmod( x, y )` is the remainder of `x / y` with the sign of `x`,
  for floats; the `%` operator does the same for ints.

```wrench
println( math::floor( 2.7 ), " ", math::floor( -2.1 ), " ", math::ceil( 2.1 ), " ", math::ceil( -2.7 ) );
println( math::trunc( 2.7 ), " ", math::trunc( -2.7 ), " ", math::abs( -3 ), " ", math::abs( -2.5 ) );
println( math::fmod( 7, 3 ), " ", math::fmod( -7.5, 2 ), " ", -7 % 3 );
var f = math::floor( 7.8 );
var i = (int)f;
println( f / 2, " ", i / 2 );
```

Output:

```text
2 -3 3 -2
2 -2 3 2.5
1 -1.5 -1
3.5 3
```

There is no `round` function. Round half up with `math::floor( x + 0.5 )`:

```wrench
var x = 2.5;
println( math::floor( x + 0.5 ), " ", (int)math::floor( -2.5 + 0.5 ) );
```

Output:

```text
3 -2
```

## math::iscale

`math::iscale( a, b, divisor )` returns `(a * b) / divisor` as an int. The
product is computed in 64 bits, so it does not overflow where `a * b`
would; the division truncates toward zero like the `/` operator. It is
meant for fixed point work on targets without a floating point unit, such
as turning an ADC reading into millivolts.

* All three arguments are converted to int (a float is truncated).
* A divisor of 0, or fewer than three arguments, returns 0.
* A quotient that does not fit 32 bits is cut to its low 32 bits.

```wrench
var adc = 3000; // a 12 bit reading
println( math::iscale( adc, 3300, 4095 ), " mV" );
println( math::iscale( 2000000000, 3, 4 ) );
println( (2000000000 * 3) / 4 );
println( math::iscale( -7, 1, 2 ), " ", math::iscale( 5, 5, 0 ), " ", math::iscale( 7, 2 ) );
```

Output:

```text
2417 mV
1500000000
426258176
-3 0 0
```

The third line shows the overflow `math::iscale` avoids: `2000000000 * 3`
does not fit an int.
