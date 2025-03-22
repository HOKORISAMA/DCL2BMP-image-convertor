#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

class DclDecoder {
private:
    static const int WIDTH = 640;
    static const int HEIGHT = 480;
    static const int BUFFER_SIZE = 0xE1000;
    uint8_t lookupTable[65536];

    bool ReadBit(int& bitBuffer, int& position, const std::vector<uint8_t>& input) {
        bool result = (input[position] & bitBuffer) != 0;
        bitBuffer >>= 1;
        if (bitBuffer == 0) {
            bitBuffer = 128;
            position++;
        }
        return result;
    }

    int ReadBits(int count, int& bitBuffer, int& position, const std::vector<uint8_t>& input) {
        int result = 0;
        int mask = 1 << (count - 1);

        while (count > 0) {
            if ((input[position] & bitBuffer) != 0)
                result |= mask;

            mask >>= 1;
            bitBuffer >>= 1;

            if (bitBuffer == 0) {
                bitBuffer = 128;
                position++;
            }

            count--;
        }

        return result;
    }

    std::vector<uint8_t> DecodeLFormat(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
        int bitBuffer = 128;
        int inputPos = 2;
        int outputPos = 0;
        
        while (true) {
            while (ReadBit(bitBuffer, inputPos, input)) {
                uint8_t value = (uint8_t)ReadBits(8, bitBuffer, inputPos, input);
                output[outputPos++] = value;
                lookupTable[outputPos & 0xFFFF] = value;
            }

            int offset = ReadBits(16, bitBuffer, inputPos, input);
            if (offset == 0)
                break;

            int length = ReadBits(4, bitBuffer, inputPos, input) + 2;

            for (int i = 0; i <= length; i++) {
                uint8_t value = lookupTable[(i + offset) & 0xFFFF];
                output[outputPos++] = value;
                lookupTable[outputPos & 0xFFFF] = value;
            }
        }

        return output;
    }

	std::vector<uint8_t> DecodePFormat(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
		// Initialize buffer with 255 instead of 0 to match decompiled code
		std::fill(output.begin(), output.end(), 255);
		
		int bitBuffer = 128;
		int inputPos = 2;
		int outputPos = 0;
		uint8_t lastR = 0, lastG = 0, lastB = 0;
		
		while (true) {
			// Process run length
			unsigned int runLength = 0;
			unsigned int command = ReadBits(2, bitBuffer, inputPos, input);
			
			if (command > 1) {
				if (command == 2) {
					runLength = ReadBits(2, bitBuffer, inputPos, input) + 2;
				}
				else {
					int bits = 3;
					while (ReadBit(bitBuffer, inputPos, input))
						bits++;

					if (bits >= 0x18) {  // 24 in hex
						runLength = -1;  // Signal to break out of main loop
					}
					else {
						runLength = ((1 << bits) - 1) + ReadBits(bits, bitBuffer, inputPos, input) - 1;
					}
				}
			}
			else {
				runLength = command;
			}
			
			// Check for termination condition
			if (runLength == (unsigned int)-1)
				break;
				
			// Advance by runLength * 3
			outputPos += runLength * 3;
			
			// Read RGB values
			uint8_t r = (uint8_t)ReadBits(8, bitBuffer, inputPos, input);
			uint8_t g = (uint8_t)ReadBits(8, bitBuffer, inputPos, input);
			uint8_t b = (uint8_t)ReadBits(8, bitBuffer, inputPos, input);
			
			// Store color
			output[outputPos] = r;
			output[outputPos + 1] = g;
			output[outputPos + 2] = b;
			
			// Save last color
			lastR = r;
			lastG = g;
			lastB = b;
			
			// Process special pixel placement
			if (ReadBit(bitBuffer, inputPos, input)) {
				int currentPos = outputPos;
				
				while (true) {
					int increment = 0;
					bool endLoop = false;
					
					switch (ReadBits(2, bitBuffer, inputPos, input)) {
						case 0:
							if (!ReadBit(bitBuffer, inputPos, input)) {
								endLoop = true;
								break;
							}
							increment = ReadBit(bitBuffer, inputPos, input) ? 1926 : 1914;
							break;
						case 1:
							increment = 1917;
							break;
						case 2:
							increment = 1920;
							break;
						case 3:
							increment = 1923;
							break;
						default:
							endLoop = true;
							break;
					}
					
					if (endLoop)
						break;
					
					currentPos += increment;
					
					// Check bounds
					if (currentPos + 2 >= (int)output.size())
						break;
					
					// Set color at new position
					output[currentPos] = r;
					output[currentPos + 1] = g;
					output[currentPos + 2] = b;
				}
			}
			
			// Move to next position
			outputPos += 3;
		}
		
		// Fill in any gaps with the last color, checking for 255 values
		for (size_t i = 0; i < output.size(); i += 3) {
			if (output[i] == 255 && output[i + 1] == 255 && output[i + 2] == 255) {
				output[i] = lastR;
				output[i + 1] = lastG;
				output[i + 2] = lastB;
			}
			else {
				lastR = output[i];
				lastG = output[i + 1];
				lastB = output[i + 2];
			}
		}
		
		return output;
	}

    // BMP file header structure
    #pragma pack(push, 1)
    struct BMPHeader {
        uint16_t signature;      // 'BM'
        uint32_t fileSize;       // Size of the BMP file in bytes
        uint16_t reserved1;      // Reserved, must be 0
        uint16_t reserved2;      // Reserved, must be 0
        uint32_t dataOffset;     // Offset to image data in bytes from beginning of file
        uint32_t headerSize;     // DIB Header size in bytes (40)
        int32_t width;           // Width of the image in pixels
        int32_t height;          // Height of the image in pixels
        uint16_t planes;         // Number of color planes
        uint16_t bitsPerPixel;   // Number of bits per pixel
        uint32_t compression;    // Compression method
        uint32_t imageSize;      // Image size in bytes
        int32_t xPixelsPerMeter; // Pixels per meter in x axis
        int32_t yPixelsPerMeter; // Pixels per meter in y axis
        uint32_t colorsUsed;     // Number of colors used
        uint32_t colorsImportant;// Number of important colors
    };
    #pragma pack(pop)

    void WriteBMPFile(const std::string& filename, const std::vector<uint8_t>& imageData) {
        BMPHeader header;
        int paddingSize = (4 - (WIDTH * 3) % 4) % 4;
        int stride = WIDTH * 3 + paddingSize;

        // Set up the BMP header
        header.signature = 0x4D42; // 'BM'
        header.fileSize = sizeof(BMPHeader) + stride * HEIGHT;
        header.reserved1 = 0;
        header.reserved2 = 0;
        header.dataOffset = sizeof(BMPHeader);
        header.headerSize = 40;
        header.width = WIDTH;
        header.height = HEIGHT; // Negative for top-down DIB (vertically flipped)
        header.planes = 1;
        header.bitsPerPixel = 24;
        header.compression = 0;
        header.imageSize = stride * HEIGHT;
        header.xPixelsPerMeter = 0;
        header.yPixelsPerMeter = 0;
        header.colorsUsed = 0;
        header.colorsImportant = 0;

        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Could not open output file: " + filename);
        }

        // Write the header
        file.write(reinterpret_cast<const char*>(&header), sizeof(BMPHeader));

        // Write the image data, top-down and with padding
        std::vector<uint8_t> padding(paddingSize, 0);
        
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                int srcOffset = (y * WIDTH + x) * 3;
                // BMP expects BGR format
                uint8_t b = imageData[srcOffset];     // B -> B
                uint8_t g = imageData[srcOffset + 1]; // G -> G
                uint8_t r = imageData[srcOffset + 2]; // R -> R
                file.write(reinterpret_cast<const char*>(&b), 1);
                file.write(reinterpret_cast<const char*>(&g), 1);
                file.write(reinterpret_cast<const char*>(&r), 1);
            }
            if (paddingSize > 0) {
                file.write(reinterpret_cast<const char*>(padding.data()), paddingSize);
            }
        }
    }

    std::vector<uint8_t> DecodeFile(const std::string& filename) {
        std::vector<uint8_t> buffer1(BUFFER_SIZE);
        std::vector<uint8_t> buffer2(BUFFER_SIZE);
        
        std::ifstream fs(filename, std::ios::binary);
        if (!fs) {
            throw std::runtime_error("Could not open file: " + filename);
        }
        
        fs.read(reinterpret_cast<char*>(buffer1.data()), BUFFER_SIZE);
        size_t bytesRead = fs.gcount();
        if (bytesRead == 0) {
            throw std::runtime_error("File is empty: " + filename);
        }

        char header = static_cast<char>(buffer1[0]);
        std::vector<uint8_t> decodedData;

        if (header == 'L') {
            decodedData = DecodeLFormat(buffer1, buffer2);
        } else if (header == 'P') {
            decodedData = DecodePFormat(buffer1, buffer2);
        } else {
            throw std::runtime_error("Unsupported DCL format");
        }

        return decodedData;
    }

public:
    DclDecoder() {
        memset(lookupTable, 0, sizeof(lookupTable));
    }

    void ConvertDirectory(const std::string& inputPath, const std::string& outputPath) {
        // Create output directory if it doesn't exist
        fs::create_directories(outputPath);
        
        // Process all DCL files in the input directory
        for (const auto& entry : fs::directory_iterator(inputPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".DCL") {
                try {
                    auto decodedData = DecodeFile(entry.path().string());
                    
                    // Create output filename
                    std::string outputFile = outputPath + "/" + 
                        entry.path().stem().string() + ".bmp";
                    
                    // Write the BMP file
                    WriteBMPFile(outputFile, decodedData);
                    
                    std::cout << "Converted: " << entry.path().filename().string() << std::endl;
                }
                catch (const std::exception& ex) {
                    std::cerr << "Error converting " << entry.path().filename().string() 
                              << ": " << ex.what() << std::endl;
                }
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: DCL2BMP <input_directory> <output_directory>" << std::endl;
        return 1;
    }

    DclDecoder decoder;
    try {
        decoder.ConvertDirectory(argv[1], argv[2]);
        std::cout << "Conversion completed successfully." << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
