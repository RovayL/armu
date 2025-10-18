int main() {
    int a = 54;
    int b = 24;
    int temp;

    while (b != 0) {
        temp = b;
        b = a % b;
        a = temp;
    }

    return a; // Expected output for (54, 24) is 6
}

