// paralelo.cpp: Versión paralela con MPI del algoritmo Bolsa de Palabras.
#include <mpi.h>

#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

// Lee un archivo completo y regresa su contenido como string.
string read_file_parallel(const string& path) {
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
vector<string> tokenize_parallel(const string& content) {
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
map<string, int> count_words_parallel(const vector<string>& tokens) {
    map<string, int> word_counts;
    for (const auto& token : tokens) {
        ++word_counts[token];
    }
    return word_counts;
}

// Serializa un set de palabras separadas por '\n'.
string serialize_vocab(const set<string>& words) {
    string result;
    for (const auto& word : words) {
        result += word;
        result.push_back('\n');
    }
    return result;
}

// Divide un string por '\n' y regresa vector de palabras.
vector<string> deserialize_vocab(const string& data) {
    vector<string> parts;
    string current;
    for (char ch : data) {
        if (ch == '\n') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// Escribe la matriz en un archivo CSV.
void write_csv_parallel(const vector<vector<int>>& matrix,
                        const vector<string>& vocabulary,
                        const vector<string>& doc_names,
                        const string& output_path) {
    ofstream output(output_path);
    if (!output.is_open()) {
        cerr << "No se pudo abrir el CSV: " << output_path << endl;
        return;
    }

    output << "document";
    for (const auto& word : vocabulary) {
        output << ',' << word;
    }
    output << '\n';

    for (size_t i = 0; i < matrix.size(); ++i) {
        output << doc_names[i];
        for (int value : matrix[i]) {
            output << ',' << value;
        }
        output << '\n';
    }
}

// Funcion principal paralela — distribuye libros entre procesos MPI.
double run_parallel(const vector<string>& document_paths) {
    int world_rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    const auto start_time = MPI_Wtime();

    // Cada proceso toma sus libros asignados (round-robin).
    vector<map<string, int>> local_counts;
    vector<int> local_indices;

    for (int idx = world_rank; idx < (int)document_paths.size(); idx += world_size) {
        const string content = read_file_parallel(document_paths[idx]);
        if (content.empty()) continue;

        const vector<string> tokens = tokenize_parallel(content);
        local_counts.push_back(count_words_parallel(tokens));
        local_indices.push_back(idx);
    }

    // Cada proceso construye su vocabulario local.
    set<string> local_vocab;
    for (const auto& doc_map : local_counts) {
        for (const auto& entry : doc_map) {
            local_vocab.insert(entry.first);
        }
    }

    // Serializa vocabulario local y envia tamano al proceso 0.
    const string local_vocab_str = serialize_vocab(local_vocab);
    const int local_vocab_size = (int)local_vocab_str.size();

    vector<int> vocab_sizes;
    if (world_rank == 0) vocab_sizes.resize(world_size);

    MPI_Gather(&local_vocab_size, 1, MPI_INT,
               world_rank == 0 ? vocab_sizes.data() : nullptr,
               1, MPI_INT, 0, MPI_COMM_WORLD);

    // Proceso 0 reune todos los vocabularios.
    vector<int> vocab_displs;
    vector<char> global_vocab_buffer;
    if (world_rank == 0) {
        vocab_displs.resize(world_size);
        int total = 0;
        for (int i = 0; i < world_size; ++i) {
            vocab_displs[i] = total;
            total += vocab_sizes[i];
        }
        global_vocab_buffer.resize(total);
    }

    MPI_Gatherv(local_vocab_str.data(), local_vocab_size, MPI_CHAR,
                world_rank == 0 ? global_vocab_buffer.data() : nullptr,
                world_rank == 0 ? vocab_sizes.data() : nullptr,
                world_rank == 0 ? vocab_displs.data() : nullptr,
                MPI_CHAR, 0, MPI_COMM_WORLD);

    // Proceso 0 construye vocabulario global y lo transmite a todos.
    string broadcast_vocab;
    if (world_rank == 0) {
        set<string> global_vocab_set;
        for (int i = 0; i < world_size; ++i) {
            if (vocab_sizes[i] == 0) continue;
            string chunk(global_vocab_buffer.begin() + vocab_displs[i],
                         global_vocab_buffer.begin() + vocab_displs[i] + vocab_sizes[i]);
            const auto words = deserialize_vocab(chunk);
            global_vocab_set.insert(words.begin(), words.end());
        }
        for (const auto& word : global_vocab_set) {
            broadcast_vocab += word;
            broadcast_vocab.push_back('\n');
        }
    }

    // Transmite vocabulario global a todos los procesos.
    int vocab_bytes = (int)broadcast_vocab.size();
    MPI_Bcast(&vocab_bytes, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (world_rank != 0) broadcast_vocab.resize(vocab_bytes);
    MPI_Bcast(broadcast_vocab.data(), vocab_bytes, MPI_CHAR, 0, MPI_COMM_WORLD);

    const vector<string> global_vocabulary = deserialize_vocab(broadcast_vocab);
    const int vocab_size = (int)global_vocabulary.size();

    // Construye indice de vocabulario para busqueda rapida.
    map<string, int> vocab_index;
    for (int i = 0; i < vocab_size; ++i) {
        vocab_index[global_vocabulary[i]] = i;
    }

    // Cada proceso construye sus filas de la matriz.
    const int local_row_count = (int)local_counts.size();
    vector<int> local_rows_flat;
    for (const auto& doc_map : local_counts) {
        vector<int> row(vocab_size, 0);
        for (const auto& entry : doc_map) {
            auto it = vocab_index.find(entry.first);
            if (it != vocab_index.end()) {
                row[it->second] = entry.second;
            }
        }
        local_rows_flat.insert(local_rows_flat.end(), row.begin(), row.end());
    }

    // Reune conteo de filas por proceso en proceso 0.
    vector<int> row_counts;
    if (world_rank == 0) row_counts.resize(world_size);
    MPI_Gather(&local_row_count, 1, MPI_INT,
               world_rank == 0 ? row_counts.data() : nullptr,
               1, MPI_INT, 0, MPI_COMM_WORLD);

    // Reune indices de documentos en proceso 0.
    vector<int> index_displs;
    vector<int> gathered_indices;
    if (world_rank == 0) {
        index_displs.resize(world_size);
        int running = 0;
        for (int i = 0; i < world_size; ++i) {
            index_displs[i] = running;
            running += row_counts[i];
        }
        gathered_indices.resize(running);
    }

    MPI_Gatherv(local_indices.data(), local_row_count, MPI_INT,
                world_rank == 0 ? gathered_indices.data() : nullptr,
                world_rank == 0 ? row_counts.data() : nullptr,
                world_rank == 0 ? index_displs.data() : nullptr,
                MPI_INT, 0, MPI_COMM_WORLD);

    // Reune filas de la matriz en proceso 0.
    vector<int> value_counts;
    vector<int> value_displs;
    vector<int> gathered_values;
    if (world_rank == 0) {
        value_counts.resize(world_size);
        value_displs.resize(world_size);
        int running = 0;
        for (int i = 0; i < world_size; ++i) {
            value_counts[i] = row_counts[i] * vocab_size;
            value_displs[i] = running;
            running += value_counts[i];
        }
        gathered_values.resize(running);
    }

    MPI_Gatherv(local_rows_flat.data(), local_row_count * vocab_size, MPI_INT,
                world_rank == 0 ? gathered_values.data() : nullptr,
                world_rank == 0 ? value_counts.data() : nullptr,
                world_rank == 0 ? value_displs.data() : nullptr,
                MPI_INT, 0, MPI_COMM_WORLD);

    // Proceso 0 ordena y escribe el CSV.
    if (world_rank == 0) {
        int total_rows = 0;
        for (int i = 0; i < world_size; ++i) total_rows += row_counts[i];

        // Ordena filas por indice original del documento.
        vector<pair<int, vector<int>>> ordered_rows;
        int offset = 0;
        for (int i = 0; i < total_rows; ++i) {
            vector<int> row(gathered_values.begin() + offset,
                            gathered_values.begin() + offset + vocab_size);
            ordered_rows.push_back({gathered_indices[i], row});
            offset += vocab_size;
        }

        sort(ordered_rows.begin(), ordered_rows.end(),
             [](const auto& a, const auto& b) { return a.first < b.first; });

        vector<string> doc_names;
        vector<vector<int>> matrix;
        for (const auto& row : ordered_rows) {
            const string& path = document_paths[row.first];
            doc_names.push_back(path.substr(path.find_last_of("/\\") + 1));
            matrix.push_back(row.second);
        }

        write_csv_parallel(matrix, global_vocabulary, doc_names, "results/bow_mpi.csv");
    }

    // Sincroniza todos los procesos antes de tomar el tiempo final.
    MPI_Barrier(MPI_COMM_WORLD);
    const double end_time = MPI_Wtime();

    // Regresa el tiempo maximo entre todos los procesos.
    double local_elapsed = end_time - start_time;
    double max_elapsed = 0.0;
    MPI_Reduce(&local_elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    return max_elapsed * 1000.0;
}