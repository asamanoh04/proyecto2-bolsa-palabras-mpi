// serial.cpp: Versión secuencial del algoritmo Bolsa de Palabras.
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// Lee un archivo completo y regresa su contenido como string.
string read_file(const string& path) {
    ifstream input(path);
    if (!input.is_open()) {
        cerr << "No se pudo abrir el archivo: " << path << endl;
        return {};
    }
    ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// Normaliza y tokeniza el contenido del libro.
// Solo conserva letras y numeros, todo en minusculas.
vector<string> tokenize(const string& content) {
    vector<string> tokens;
    string current_token;

    for (unsigned char raw : content) {
        const char lower = static_cast<char>(tolower(raw));
        if (isalnum(lower)) {
            current_token.push_back(lower);
        } else {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        }
    }
    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }
    return tokens;
}

// Cuenta la frecuencia de cada palabra en un documento.
map<string, int> count_words(const vector<string>& tokens) {
    map<string, int> word_counts;
    for (const auto& token : tokens) {
        ++word_counts[token];
    }
    return word_counts;
}

// Construye el vocabulario global ordenado usando todos los documentos.
vector<string> build_vocabulary(const vector<map<string, int>>& document_counts) {
    map<string, int> unique_words;
    for (const auto& doc_map : document_counts) {
        for (const auto& entry : doc_map) {
            unique_words[entry.first] = 0;
        }
    }
    vector<string> vocabulary;
    for (const auto& entry : unique_words) {
        vocabulary.push_back(entry.first);
    }
    return vocabulary;
}

// Construye la matriz bolsa de palabras.
vector<vector<int>> build_matrix(
    const vector<map<string, int>>& document_counts,
    const vector<string>& vocabulary) {

    vector<vector<int>> matrix;
    for (const auto& doc_map : document_counts) {
        vector<int> row;
        for (const auto& word : vocabulary) {
            auto it = doc_map.find(word);
            row.push_back(it != doc_map.end() ? it->second : 0);
        }
        matrix.push_back(row);
    }
    return matrix;
}

// Escribe la matriz en un archivo CSV.
void write_csv(const vector<vector<int>>& matrix,
               const vector<string>& vocabulary,
               const vector<string>& doc_names,
               const string& output_path) {
    ofstream output(output_path);
    if (!output.is_open()) {
        cerr << "No se pudo abrir el CSV: " << output_path << endl;
        return;
    }

    // Encabezado con nombres de palabras.
    output << "document";
    for (const auto& word : vocabulary) {
        output << ',' << word;
    }
    output << '\n';

    // Una fila por libro.
    for (size_t i = 0; i < matrix.size(); ++i) {
        output << doc_names[i];
        for (int value : matrix[i]) {
            output << ',' << value;
        }
        output << '\n';
    }
}

// Funcion principal del serial — recibe rutas de libros y regresa tiempo en ms.
double run_serial(const vector<string>& document_paths) {
    const auto start_time = chrono::steady_clock::now();

    vector<map<string, int>> document_counts;
    vector<string> doc_names;

    // Procesa cada libro uno por uno.
    for (const auto& path : document_paths) {
        const string content = read_file(path);
        if (content.empty()) continue;

        const vector<string> tokens = tokenize(content);
        document_counts.push_back(count_words(tokens));
        doc_names.push_back(path.substr(path.find_last_of("/\\") + 1));
    }

    if (document_counts.empty()) {
        cerr << "No se proceso ningun documento." << endl;
        return 0.0;
    }

    const vector<string> vocabulary = build_vocabulary(document_counts);
    const vector<vector<int>> matrix = build_matrix(document_counts, vocabulary);

    write_csv(matrix, vocabulary, doc_names, "results/bow_serial.csv");

    const auto end_time = chrono::steady_clock::now();
    return chrono::duration<double, milli>(end_time - start_time).count();
}