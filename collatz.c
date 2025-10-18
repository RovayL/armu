// Calculates the number of steps for the Collatz sequence to reach 1.
int main() {
    int n = 6;
    int steps = 0;

    if (n <= 0) {
        return 0;
    }

    while (n != 1) {
        if (n % 2 == 0) {
            n = n / 2;
        } else {
            n = 3 * n + 1;
        }
        steps = steps + 1;
    }

    return steps; // For n=6, the sequence is 6, 3, 10, 5, 16, 8, 4, 2, 1 (8 steps)
}
