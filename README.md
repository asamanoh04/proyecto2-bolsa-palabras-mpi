# Bolsa de Palabras con MPI
**Proyecto Clausura — Cómputo Paralelo y en la Nube**  
ITAM · Semestre VIII · 2026

---

## ¿Qué hace este proyecto?

Implementa el algoritmo **Bag of Words (Bolsa de Palabras)** en dos versiones:

- **Serial** — procesa los libros uno por uno
- **Paralela con MPI** — distribuye los libros entre múltiples procesos

El programa descarga libros automáticamente desde [Project Gutenberg](https://gutenberg.org) vía la API de [Gutendex](https://gutendex.com), cuenta la frecuencia de cada palabra en cada libro, y genera una matriz CSV donde cada fila es un libro y cada columna es una palabra.

Al final calcula e imprime el **speed-up** entre la versión serial y la paralela.

---

## Estructura del proyecto

```
proyecto2-bolsa-palabras-mpi/
├── src/
│   ├── main.cpp        ← descarga libros, orquesta serial/paralelo, calcula speed-up
│   ├── serial.cpp      ← versión serial del algoritmo
│   └── paralelo.cpp    ← versión paralela con MPI
├── data/
│   └── books/          ← libros descargados (generado en tiempo de ejecución)
├── results/
│   ├── bow_serial.csv  ← matriz generada por la versión serial
│   └── bow_mpi.csv     ← matriz generada por la versión paralela
├── build/              ← ejecutable compilado
├── Makefile
└── README.md
```

---

## Requisitos

- Windows con **MS-MPI** instalado ([descargar aquí](https://learn.microsoft.com/en-us/message-passing-interface/microsoft-mpi))
- **g++** (MinGW / MSYS2)
- **libcurl** (instalada via MSYS2: `pacman -S mingw-w64-x86_64-curl`)
- **mingw32-make**

---

## Compilar

```powershell
mingw32-make
```

## Correr

```powershell
mpiexec -n <procesos> .\build\bow_app.exe <procesos> <num_libros>
```

**Ejemplos:**

```powershell
mpiexec -n 2 .\build\bow_app.exe 2 3    # 2 procesos, 3 libros
mpiexec -n 4 .\build\bow_app.exe 4 6    # 4 procesos, 6 libros
mpiexec -n 8 .\build\bow_app.exe 8 10   # 8 procesos, 10 libros
```

## Limpiar

```powershell
mingw32-make clean
```

---

## ¿Cómo funciona?

### Serial
1. Lee cada libro uno por uno
2. Tokeniza y cuenta palabras con `map<string, int>`
3. Construye vocabulario global y matriz
4. Guarda `results/bow_serial.csv`

### Paralelo (MPI)
1. Proceso 0 descarga los libros y transmite rutas con `MPI_Bcast`
2. Cada proceso toma sus libros asignados (round-robin)
3. Cada proceso construye su vocabulario local
4. Se reúnen vocabularios con `MPI_Gather` / `MPI_Gatherv`
5. Proceso 0 construye vocabulario global y lo transmite con `MPI_Bcast`
6. Cada proceso construye sus filas de la matriz
7. Se reúnen filas con `MPI_Gatherv`
8. Proceso 0 ordena y guarda `results/bow_mpi.csv`
9. Se mide tiempo máximo entre procesos con `MPI_Reduce`

---

## Resultados ejemplo

```
Tiempo serial:   424.008 ms
Tiempo paralelo: 321.406 ms
Speed-up:        1.32x ✓
```

---

## Autores

- André — ITAM Semestre VIII
