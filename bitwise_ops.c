int main() {
    int a = 60;  // 0011 1100
    int b = 13;  // 0000 1101

    int and_result = a & b;  // 12 -> 0000 1100
    int or_result = a | b;   // 61 -> 0011 1101
    int xor_result = a ^ b;  // 49 -> 0011 0001
    int not_result = ~a;     // This will be a negative number
    int lshift_result = a << 2; // 240 -> 1111 0000
    int rshift_result = a >> 2; // 15  -> 0000 1111

    // Return a single, predictable result
    return lshift_result; // Expected: 240
}
