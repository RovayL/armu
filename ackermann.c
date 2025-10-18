int ack(int m, int n) {
    if (m == 0) {
        return n + 1;
    } else if (n == 0) {
        return ack(m - 1, 1);
    } else {
        return ack(m - 1, ack(m, n - 1));
    }
}

int main() {
    // ack(3, 2) = 29. Values higher than this will likely overflow or take too long.
    return ack(3, 2);
}
