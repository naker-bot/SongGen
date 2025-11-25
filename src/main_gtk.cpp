#include "GtkRenderer.h"
#include <gtk/gtk.h>
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "🎵 SongGen - GTK Native GUI\n";
    std::cout << "=============================\n\n";
    
    // GTK initialisieren
    gtk_init(&argc, &argv);
    
    // Renderer erstellen
    GtkRenderer renderer;
    
    if (!renderer.initialize()) {
        std::cerr << "❌ Initialization failed\n";
        return 1;
    }
    
    // GUI starten
    renderer.run();
    
    return 0;
}
