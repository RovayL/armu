int main() {
    int base = 3;
    int exp = 4;
    int result = 1;

    while (exp > 0) {
        result = result * base;
        exp = exp - 1;
    }

    return result; // Expected output for 3^4 is 81
}
