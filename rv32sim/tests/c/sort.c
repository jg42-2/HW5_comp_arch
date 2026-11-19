// sort.c — Programa en C compilado con clang para RV32I y ejecutado en el
// simulador. Ordena un arreglo con insertion sort, lo imprime usando ecall
// y devuelve la suma de los elementos como código de salida.

static void print_str(const char* s) {
    register const char* a0 asm("a0") = s;
    register int a7 asm("a7") = 4;
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void print_int(int v) {
    register int a0 asm("a0") = v;
    register int a7 asm("a7") = 1;
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void print_array(const char* label, const int* a, int n) {
    print_str(label);
    for (int i = 0; i < n; i++) {
        print_int(a[i]);
        print_str(i + 1 < n ? ", " : "\n");
    }
}

int datos[] = {42, -7, 15, 0, 99, 3, -20, 8};

int main(void) {
    const int n = sizeof(datos) / sizeof(datos[0]);
    print_array("Antes:   ", datos, n);

    for (int i = 1; i < n; i++) {           // insertion sort
        int key = datos[i], j = i - 1;
        while (j >= 0 && datos[j] > key) {
            datos[j + 1] = datos[j];
            j--;
        }
        datos[j + 1] = key;
    }

    print_array("Despues: ", datos, n);
    int suma = 0;
    for (int i = 0; i < n; i++) suma += datos[i];
    return suma;   // 140
}
