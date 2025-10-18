int main() {
    int n = 29; // Number to check
    int i = 2;

    if (n <= 1) {
        return 0; // 0 and 1 are not prime
    }

    while (i * i <= n) {
        if (n % i == 0) {
            return 0; // Not prime
        }
        i = i + 1;
    }

    return 1; // Is prime
}
