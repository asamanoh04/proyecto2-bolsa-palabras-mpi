// main.cpp: Orquesta la descarga de libros, corre serial y paralelo, calcula speed-up.
#include <mpi.h>
#include <curl/curl.h>
#include <fstream>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// Declaraciones de funciones definidas en serial.cpp y paralelo.cpp
double run_serial(const vector<string>& document_paths);
double run_parallel(const vector<string>& document_paths);

// Callback que libcurl usa para escribir los datos descargados en un string.
size_t write_callback(void* contents, size_t size, size_t nmemb, string* output) {
    output->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Descarga el contenido de una URL y lo regresa como string.
string download_url(const string& url) {
    CURL* curl = curl_easy_init();
    string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return response;
}

// Consulta la API de Gutendex y regresa n URLs de libros en ingles random.
vector<string> get_random_book_urls(int n) {
    vector<string> urls;

    srand(time(nullptr));
    int page = rand() % 20 + 1;

    const string api_url = "https://gutendex.com/books/?languages=en&mime_type=text%2Fplain&page="
                           + to_string(page);

    cout << "Consultando API de Gutenberg..." << endl;
    const string response = download_url(api_url);

    if (response.empty()) {
        cerr << "No se pudo conectar a la API de Gutenberg." << endl;
        return urls;
    }

    // Busca URLs de texto plano en el JSON.
    size_t pos = 0;
    while ((int)urls.size() < n) {
        pos = response.find("text/plain; charset=us-ascii", pos);
        if (pos == string::npos) break;

        size_t url_pos = response.find("\":\"", pos);
        if (url_pos == string::npos) break;

        url_pos += 3;
        size_t url_end = response.find("\"", url_pos);
        if (url_end == string::npos) break;

        string url = response.substr(url_pos, url_end - url_pos);
        urls.push_back(url);
        pos = url_end;
    }

    return urls;
}

// Descarga n libros de Gutenberg y los guarda en data/books/.
vector<string> download_books(int n) {
    vector<string> local_paths;

    filesystem::create_directories("data/books");

    const vector<string> urls = get_random_book_urls(n);
    if (urls.empty()) {
        cerr << "No se obtuvieron URLs de libros." << endl;
        return local_paths;
    }

    for (int i = 0; i < (int)urls.size(); ++i) {
        const string filename = "data/books/libro_" + to_string(i + 1) + ".txt";
        cout << "Descargando libro " << (i + 1) << ": " << urls[i] << endl;

        const string content = download_url(urls[i]);
        if (content.empty()) {
            cerr << "No se pudo descargar: " << urls[i] << endl;
            continue;
        }

        ofstream file(filename);
        file << content;
        file.close();

        local_paths.push_back(filename);
        cout << "  Guardado en: " << filename << endl;
    }

    return local_paths;
}

int main(int argc, char** argv) {
    MPI_Init(nullptr, nullptr);

    int world_rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    if (argc < 3) {
        if (world_rank == 0) {
            cerr << "Uso: mpiexec -n <procesos> .\\build\\bow_app.exe <procesos> <num_libros>" << endl;
            cerr << "Ejemplo: mpiexec -n 4 .\\build\\bow_app.exe 4 6" << endl;
        }
        MPI_Finalize();
        return 1;
    }

    const int num_processes = stoi(argv[1]);
    const int num_books = stoi(argv[2]);

    int world_size = 1;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_rank == 0 && num_processes != world_size) {
        cerr << "Advertencia: se pidieron " << num_processes
             << " procesos pero se lanzaron " << world_size << endl;
    }

    // Solo el proceso 0 descarga los libros.
    vector<string> document_paths;
    if (world_rank == 0) {
        cout << "Descargando " << num_books << " libros de Project Gutenberg..." << endl;
        document_paths = download_books(num_books);

        if (document_paths.empty()) {
            cerr << "No se descargo ningun libro." << endl;
            MPI_Finalize();
            return 1;
        }

        cout << "Libros descargados: " << document_paths.size() << endl;
    }

    // Transmite las rutas de los libros a todos los procesos.
    int num_docs = (int)document_paths.size();
    MPI_Bcast(&num_docs, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (world_rank != 0) document_paths.resize(num_docs);

    for (int i = 0; i < num_docs; ++i) {
        int path_size = (int)document_paths[i].size();
        MPI_Bcast(&path_size, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (world_rank != 0) document_paths[i].resize(path_size);
        MPI_Bcast(document_paths[i].data(), path_size, MPI_CHAR, 0, MPI_COMM_WORLD);
    }

    // Corre version serial (solo proceso 0).
    double serial_time = 0.0;
    if (world_rank == 0) {
        cout << "\nCorriendo version serial..." << endl;
        serial_time = run_serial(document_paths);
        cout << "Tiempo serial: " << serial_time << " ms" << endl;
    }

    // Sincroniza antes de correr paralelo.
    MPI_Barrier(MPI_COMM_WORLD);

    // Corre version paralela (todos los procesos).
    if (world_rank == 0) {
        cout << "Corriendo version paralela con " << world_size << " procesos..." << endl;
    }
    double parallel_time = run_parallel(document_paths);

    // Imprime resultados y speed-up.
    if (world_rank == 0) {
        const double speedup = (parallel_time > 0.0) ? (serial_time / parallel_time) : 0.0;
        cout << "\n==== Resultados ====" << endl;
        cout << "Tiempo serial:   " << serial_time << " ms" << endl;
        cout << "Tiempo paralelo: " << parallel_time << " ms" << endl;
        cout << "Speed-up:        " << speedup << "x" << endl;

        if (speedup >= 1.2) {
            cout << "Speed-up mayor a 1.2x" << endl;
        } else {
            cout << "Speed-up menor a 1.2x, revisar implementacion." << endl;
        }
    }

    MPI_Finalize();
    return 0;
}