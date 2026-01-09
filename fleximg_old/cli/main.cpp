// Image Transform CLI Tool
// Command-line interface for image processing with the native build.
//
// Usage:
//   ./imgproc input.png -o output.png [options]
//
// Options:
//   -o, --output <file>     Output file path (required)
//   --brightness <value>    Apply brightness filter (0.0-2.0, default 1.0)
//   --grayscale             Convert to grayscale
//   --alpha <value>         Set alpha value (0.0-1.0)
//   --verbose               Show verbose output
//   --help                  Show this help message

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#include "stb_image.h"
#include "stb_image_write.h"

#include "fleximg/image_buffer.h"
#include "fleximg/eval_result.h"
#include "fleximg/operators.h"
#include "fleximg/image_types.h"

using namespace FLEXIMG_NAMESPACE;

// Command-line options
struct Options {
    std::string inputFile;
    std::string outputFile;
    bool verbose = false;

    // Filter options
    bool applyBrightness = false;
    float brightness = 1.0f;

    bool applyGrayscale = false;

    bool applyAlpha = false;
    float alpha = 1.0f;
};

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <input> -o <output> [options]\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output <file>     Output file path (required)\n"
              << "  --brightness <value>    Apply brightness filter (0.0-2.0)\n"
              << "  --grayscale             Convert to grayscale\n"
              << "  --alpha <value>         Set alpha value (0.0-1.0)\n"
              << "  --verbose               Show verbose output\n"
              << "  --help                  Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName << " input.png -o output.png --brightness 1.2\n"
              << "  " << programName << " input.jpg -o output.png --grayscale\n";
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
    std::memcpy(buffer.data, inputData, width * height * 4);
    stbi_image_free(inputData);

    // Wrap in EvalResult
    EvalResult evalResult(std::move(buffer), Point2f(0, 0));

    // Apply filters
    RenderRequest request;
    request.width = width;
    request.height = height;
    request.originX = width / 2.0f;
    request.originY = height / 2.0f;

    if (opts.applyBrightness) {
        if (opts.verbose) {
            std::cout << "Applying brightness: " << opts.brightness << "\n";
        }
        auto op = OperatorFactory::createFilterOperator("brightness", {opts.brightness});
        if (op) {
            OperatorInput input(evalResult);
            evalResult = op->apply({input}, request);
        }
    }

    if (opts.applyGrayscale) {
        if (opts.verbose) {
            std::cout << "Applying grayscale\n";
        }
        auto op = OperatorFactory::createFilterOperator("grayscale", {});
        if (op) {
            OperatorInput input(evalResult);
            evalResult = op->apply({input}, request);
        }
    }

    if (opts.applyAlpha) {
        if (opts.verbose) {
            std::cout << "Applying alpha: " << opts.alpha << "\n";
        }
        auto op = OperatorFactory::createFilterOperator("alpha", {opts.alpha});
        if (op) {
            OperatorInput input(evalResult);
            evalResult = op->apply({input}, request);
        }
    }

    // Ensure output is RGBA8_Straight for PNG output
    if (evalResult.buffer.formatID != PixelFormatIDs::RGBA8_Straight) {
        ViewPort view = evalResult.buffer.view();
        ImageBuffer converted = view.toImageBuffer(PixelFormatIDs::RGBA8_Straight);
        evalResult = EvalResult(std::move(converted), evalResult.origin);
    }

    // Write output image
    int result = stbi_write_png(opts.outputFile.c_str(),
                                evalResult.buffer.width, evalResult.buffer.height,
                                4,  // RGBA
                                evalResult.buffer.data,
                                evalResult.buffer.stride);

    if (result == 0) {
        std::cerr << "Error: Failed to write output: " << opts.outputFile << "\n";
        return 1;
    }

    if (opts.verbose) {
        std::cout << "Written: " << opts.outputFile << "\n";
    }

    return 0;
}
