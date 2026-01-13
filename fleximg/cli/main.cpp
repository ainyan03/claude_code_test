// fleximg CLI Tool
// Command-line interface for image processing.
//
// Usage:
//   ./imgproc input.png -o output.png [options]
//
// Options:
//   -o, --output <file>     Output file path (required)
//   --brightness <value>    Apply brightness filter (-1.0 to 1.0)
//   --grayscale             Convert to grayscale
//   --blur <radius>         Apply box blur (radius in pixels)
//   --alpha <value>         Set alpha value (0.0-1.0)
//   --verbose               Show verbose output
//   --help                  Show this help message
//
// Build:
//   g++ -std=c++17 -O2 -I../src -I../third_party \
//       main.cpp stb_impl.cpp ../src/fleximg/*.cpp ../src/fleximg/operations/*.cpp \
//       -o imgproc

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#include "stb_image.h"
#include "stb_image_write.h"

#include "fleximg/image/image_buffer.h"
#include "fleximg/image/viewport.h"
#include "fleximg/operations/filters.h"

using namespace fleximg;

// Command-line options
struct Options {
    std::string inputFile;
    std::string outputFile;
    bool verbose = false;

    // Filter options
    bool applyBrightness = false;
    float brightness = 0.0f;

    bool applyGrayscale = false;

    bool applyBlur = false;
    int blurRadius = 3;

    bool applyAlpha = false;
    float alpha = 1.0f;
};

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <input> -o <output> [options]\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output <file>     Output file path (required)\n"
              << "  --brightness <value>    Apply brightness filter (-1.0 to 1.0)\n"
              << "  --grayscale             Convert to grayscale\n"
              << "  --blur <radius>         Apply box blur (radius in pixels)\n"
              << "  --alpha <value>         Set alpha value (0.0-1.0)\n"
              << "  --verbose               Show verbose output\n"
              << "  --help                  Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName << " input.png -o output.png --brightness 0.2\n"
              << "  " << programName << " input.jpg -o output.png --grayscale\n"
              << "  " << programName << " input.png -o output.png --blur 5\n";
}

bool parseArgs(int argc, char* argv[], Options& opts) {
    if (argc < 2) {
        return false;
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            exit(0);
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --output requires a file path\n";
                return false;
            }
            opts.outputFile = argv[++i];
        } else if (arg == "--brightness") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --brightness requires a value\n";
                return false;
            }
            opts.applyBrightness = true;
            opts.brightness = std::stof(argv[++i]);
        } else if (arg == "--grayscale") {
            opts.applyGrayscale = true;
        } else if (arg == "--blur") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --blur requires a radius value\n";
                return false;
            }
            opts.applyBlur = true;
            opts.blurRadius = std::stoi(argv[++i]);
        } else if (arg == "--alpha") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --alpha requires a value\n";
                return false;
            }
            opts.applyAlpha = true;
            opts.alpha = std::stof(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return false;
        } else {
            // Positional argument = input file
            if (opts.inputFile.empty()) {
                opts.inputFile = arg;
            } else {
                std::cerr << "Error: Multiple input files not supported\n";
                return false;
            }
        }
    }

    if (opts.inputFile.empty()) {
        std::cerr << "Error: Input file required\n";
        return false;
    }
    if (opts.outputFile.empty()) {
        std::cerr << "Error: Output file required (-o)\n";
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    Options opts;

    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

    // Load input image
    int width, height, channels;
    unsigned char* inputData = stbi_load(opts.inputFile.c_str(), &width, &height, &channels, 4);

    if (!inputData) {
        std::cerr << "Error: Failed to load image: " << opts.inputFile << "\n";
        std::cerr << "  Reason: " << stbi_failure_reason() << "\n";
        return 1;
    }

    if (opts.verbose) {
        std::cout << "Loaded: " << opts.inputFile << "\n";
        std::cout << "  Size: " << width << "x" << height << "\n";
        std::cout << "  Channels: " << channels << " (loaded as 4)\n";
    }

    // Create ImageBuffer from loaded data
    ImageBuffer buffer(width, height, PixelFormatIDs::RGBA8_Straight);
    std::memcpy(buffer.data(), inputData, width * height * 4);
    stbi_image_free(inputData);

    // Apply filters (use double buffering)
    ImageBuffer tempBuffer(width, height, PixelFormatIDs::RGBA8_Straight);
    ViewPort srcView = buffer.view();
    ViewPort dstView = tempBuffer.view();
    bool useTemp = false;

    auto swapBuffers = [&]() {
        useTemp = !useTemp;
        if (useTemp) {
            srcView = tempBuffer.view();
            dstView = buffer.view();
        } else {
            srcView = buffer.view();
            dstView = tempBuffer.view();
        }
    };

    if (opts.applyBrightness) {
        if (opts.verbose) {
            std::cout << "Applying brightness: " << opts.brightness << "\n";
        }
        filters::brightness(dstView, srcView, opts.brightness);
        swapBuffers();
    }

    if (opts.applyGrayscale) {
        if (opts.verbose) {
            std::cout << "Applying grayscale\n";
        }
        filters::grayscale(dstView, srcView);
        swapBuffers();
    }

    if (opts.applyBlur) {
        if (opts.verbose) {
            std::cout << "Applying blur: radius=" << opts.blurRadius << "\n";
        }
        filters::boxBlur(dstView, srcView, opts.blurRadius);
        swapBuffers();
    }

    if (opts.applyAlpha) {
        if (opts.verbose) {
            std::cout << "Applying alpha: " << opts.alpha << "\n";
        }
        filters::alpha(dstView, srcView, opts.alpha);
        swapBuffers();
    }

    // Get final result (srcView points to the result after last swap)
    ImageBuffer& result = useTemp ? tempBuffer : buffer;

    // Write output image
    int writeResult = stbi_write_png(opts.outputFile.c_str(),
                                     result.width(), result.height(),
                                     4,  // RGBA
                                     result.data(),
                                     static_cast<int>(result.stride()));

    if (writeResult == 0) {
        std::cerr << "Error: Failed to write output: " << opts.outputFile << "\n";
        return 1;
    }

    if (opts.verbose) {
        std::cout << "Written: " << opts.outputFile << "\n";
    }

    return 0;
}
