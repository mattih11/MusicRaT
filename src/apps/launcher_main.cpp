#include <musicrat/launcher/audio_format_validation.hpp>

#include <commrat/launcher/process_launcher.hpp>

int main(int argc, char** argv) {
    return commrat::ProcessLauncher::main(
        argc, argv, musicrat::launcher::validate_application);
}