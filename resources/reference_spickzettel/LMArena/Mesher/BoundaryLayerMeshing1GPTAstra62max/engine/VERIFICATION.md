# What is certified

The two shipped meshes have an independent exact-arithmetic audit in `tests/certify_dyadic.py`. This certificate applies to the coordinates and connectivity in those particular exports. Interactive rebuilds run the C++ binary64 acceptance checks; they are not automatically re-certified by the offline integer checker.

## Exact representation

Every finite binary64 coordinate is a dyadic rational. Parsing the 17-significant-digit JSON export round-trips the original double. Taking the largest power-of-two denominator S across all coordinates represents every vertex as an integer vector P divided by S. The audit uses arbitrary-size integers from this point onward.

## Discrete closure and volume

For an oriented triangle (a,b,c), twice its area vector is `(b-a) cross (c-a)`. The sum over a face equals its boundary sum of consecutive vertex cross products. Every edge in a closed, consistently oriented cell occurs twice in opposite directions. Hence the exact vector sum vanishes. This is a topological identity, not an empirical tolerance estimate. The checker explicitly evaluates the three integer components for every cell.

Both owners triangulate a quadrilateral from its smallest global vertex index. Let a cell contain n vertices with integer-coordinate sum T. For each oriented face triangle, the sign of its tetrahedron with the cell centroid is exactly the sign of

```
(n*A - T) dot ((B-A) cross (C-A)).
```

Its physical volume is this integer divided by `6*n*S^3`. All of these integers must be strictly positive. Since 120 is divisible by 4, 5, 6, and 8, all cell volumes share the integer denominator `720*S^3` after summation. The checker compares the total exactly against the analytic fixture volume, not a rounded volume stored in the report.

The binary64 solver separately measures the requested relative Gauss residual with `V^(2/3)` normalization. A value of order 1e-15 is a floating-point cancellation residual, not a geometric hole.

## Exact mixed-element quality gate

For a corner basis `(a,b,c)` let D be its determinant and let P be the product of the three squared edge norms. For optimum `sqrt(u/v)`, the positive normalized Jacobian threshold 1/4 is equivalent to

```
D > 0  and  16*v*D^2 > u*P.
```

Above the element optimum, the reflected normalization `1 + opt - SJ` is at least `opt`, which exceeds 1/4 for every supported element. Thus the comparison is valid on both positive branches. The squared optima are exactly 1, 3/4, and 1/2. Every corner basis, every pyramid apex triple, and the constant-normal fictitious base hex is checked without a square root or roundoff.

## Continuous prism determinant

Across the unit triangular reference section the determinant is affine. Its minimum at fixed height is attained at one of the three triangle vertices. For each of those vertices, the two in-plane derivatives interpolate linearly through height, while the extrusion derivative is the fixed top-minus-bottom vector d. The determinant is therefore a quadratic `A*t*t + B*t + C` on `[0,1]`.

The minimum is the smaller endpoint value unless `A>0` and `-2*A<B<0`, in which case the value is exactly `(4*A*C-B^2)/(4*A)`. A bound on the product of in-plane derivative norms follows from the maximum endpoint norms. The checker squares the positive minimum and compares it to `9/400` times the squared norm product, establishing the dimensionless 0.15 gate throughout the wedge. At interior triangular locations, linearity of the determinant and the triangle inequality for the interpolated extrusion vector preserve this lower bound.

## Conformity and scope

The native code checks opposite shared face cycles, known exterior faces, and all vertex-on-edge incidences in spatial bins. The finite-area extraction checks closed exterior edge incidence and connected vertex links. The volume complex is built by subdividing known grid cells and a shared-ring shell extrusion. In exact arithmetic, positive simplices throughout an oriented triangulated ball with an injective fixed exterior imply degree one inside that exterior. This argument uses the constructed fixture topology; positive volumes alone would not establish non-overlap of arbitrary input cells.

These checks are not a general CAD intersection solver, a formal proof of a C++ compiler, or a certification of arbitrary trilinear hexahedral finite-element mappings. The volume interpretation for warped faces is the explicitly stated canonical piecewise-linear complex.