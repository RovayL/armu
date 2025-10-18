// Recursive function to compute the nth Fibonacci number
int fibonacci(int n) {
    if (n <= 1) {
        return n; // Base cases: F(0) = 0, F(1) = 1
    } else {
        return fibonacci(n - 1) + fibonacci(n - 2); // Recursive step
    }
}

int main() {
    int n = 20; // The desired Fibonacci number index
    int result;

    result = fibonacci(n); // Call the recursive function


    return result; // Simply return the computed fibonacci value
}
