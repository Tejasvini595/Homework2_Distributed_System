# Report Format

## Results Table

Speed-up $S(P) = T_1 / T_P$

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Small  | 1.00 | | | |
| Medium | 1.00 | | | |
| Large  | 1.00 | | | |
| Very large | 1.00 | | | |

*(One table per problem)*

## Also can include

- **Runtime table** — raw seconds.
- **Efficiency** — $E(P) = S(P)/P$.
- **Speed-up vs. $P$ graph**
- **Communication vs. computation time** — % spent in MPI calls.
- **Correctness** — small test cases vs. sequential output.
- **Implementation notes** — compile flags, partition used, handling non-divisible sizes.
- **Conclusion** — brief summary of trends.
