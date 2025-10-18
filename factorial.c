int main() {
    int n = 5; // Input value
    int result = 1;
    int i = 1;

    while (i <= n) {
        result = result * i;
        i = i + 1;
    }

    return result; // Expected output for n=5 is 120
}
