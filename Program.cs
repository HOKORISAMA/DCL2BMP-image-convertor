using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
using SixLabors.ImageSharp.Processing;
using System;
using System.IO;
using System.Runtime.InteropServices;

namespace DclConverter
{
    public class DclDecoder
    {
        private const int WIDTH = 640;
        private const int HEIGHT = 480;
        private const int BUFFER_SIZE = 0xE1000;
        private readonly byte[] lookupTable = new byte[65536];
        
        public void ConvertDirectory(string inputPath, string outputPath)
        {
            Directory.CreateDirectory(outputPath);
            
            foreach (var file in Directory.GetFiles(inputPath, "*.DCL"))
            {
                try
                {
                    using var image = DecodeFile(file);
                    // Apply the corrections
                    image.Mutate(x => x
                        .Flip(FlipMode.Vertical));
                        
                    var outputFile = Path.Combine(outputPath, 
                        Path.GetFileNameWithoutExtension(file) + ".bmp");
                    image.Save(outputFile);
                    Console.WriteLine($"Converted: {Path.GetFileName(file)}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error converting {Path.GetFileName(file)}: {ex.Message}");
                }
            }
        }

        private Image<Rgb24> DecodeFile(string filename)
        {
            var buffer1 = new byte[BUFFER_SIZE];
            var buffer2 = new byte[BUFFER_SIZE];
            
            using (var fs = File.OpenRead(filename))
            {
                fs.Read(buffer1, 0, BUFFER_SIZE);
            }

            var header = (char)buffer1[0];
            byte[] decodedData;

            switch (header)
            {
                case 'L':
                    decodedData = DecodeLFormat(buffer1, buffer2);
                    break;
                case 'P':
                    decodedData = DecodePFormat(buffer1, buffer2);
                    break;
                default:
                    throw new InvalidDataException("Unsupported DCL format");
            }

            // Create ImageSharp image from decoded data
            var image = new Image<Rgb24>(WIDTH, HEIGHT);

            for (int y = 0; y < HEIGHT; y++)
            {
                for (int x = 0; x < WIDTH; x++)
                {
                    int offset = (y * WIDTH + x) * 3;
                    // Swap BGR to RGB here
                    image[x, y] = new Rgb24(
                        decodedData[offset + 2], // B -> R
                        decodedData[offset + 1], // G -> G
                        decodedData[offset]      // R -> B
                    );
                }
            }

            return image;
        }

        private byte[] DecodeLFormat(byte[] input, byte[] output)
        {
            int bitBuffer = 128;
            int inputPos = 2;
            int outputPos = 0;
            
            while (true)
            {
                while (ReadBit(ref bitBuffer, ref inputPos, input))
                {
                    byte value = (byte)ReadBits(8, ref bitBuffer, ref inputPos, input);
                    output[outputPos++] = value;
                    lookupTable[outputPos & 0xFFFF] = value;
                }

                int offset = ReadBits(16, ref bitBuffer, ref inputPos, input);
                if (offset == 0)
                    break;

                int length = ReadBits(4, ref bitBuffer, ref inputPos, input) + 2;

                for (int i = 0; i <= length; i++)
                {
                    byte value = lookupTable[(i + offset) & 0xFFFF];
                    output[outputPos++] = value;
                    lookupTable[outputPos & 0xFFFF] = value;
                }
            }

            return output;
        }

        private byte[] DecodePFormat(byte[] input, byte[] output)
        {
            // Initialize buffer with 255 instead of 0 to match decompiled code
            for (int i = 0; i < output.Length; i++)
            {
                output[i] = 255;
            }
            
            int bitBuffer = 128;
            int inputPos = 2;
            int outputPos = 0;
            byte lastR = 0, lastG = 0, lastB = 0;
            
            while (true)
            {
                // Process run length
                uint runLength = 0;
                uint command = (uint)ReadBits(2, ref bitBuffer, ref inputPos, input);
                
                if (command > 1)
                {
                    if (command == 2)
                    {
                        runLength = (uint)(ReadBits(2, ref bitBuffer, ref inputPos, input) + 2);
                    }
                    else
                    {
                        uint bits = 3;
                        while (ReadBit(ref bitBuffer, ref inputPos, input))
                            bits++;

                        if (bits >= 0x18) // 24 in hex
                        {
                            runLength = uint.MaxValue; // Signal to break out of main loop
                        }
                        else
                        {
                            runLength = (uint)((1 << (int)bits) - 1 + ReadBits((int)bits, ref bitBuffer, ref inputPos, input) - 1);
                        }
                    }
                }
                else
                {
                    runLength = command;
                }
                
                // Check for termination condition
                if (runLength == uint.MaxValue)
                    break;
                    
                // Advance by runLength * 3
                outputPos += (int)runLength * 3;
                
                // Ensure we don't exceed the buffer
                if (outputPos >= WIDTH * HEIGHT * 3)
                    break;
                    
                // Read RGB values
                byte r = (byte)ReadBits(8, ref bitBuffer, ref inputPos, input);
                byte g = (byte)ReadBits(8, ref bitBuffer, ref inputPos, input);
                byte b = (byte)ReadBits(8, ref bitBuffer, ref inputPos, input);
                
                // Store color
                output[outputPos] = r;
                output[outputPos + 1] = g;
                output[outputPos + 2] = b;
                
                // Save last color
                lastR = r;
                lastG = g;
                lastB = b;
                
                // Process special pixel placement
                if (ReadBit(ref bitBuffer, ref inputPos, input))
                {
                    int currentPos = outputPos;
                    
                    while (true)
                    {
                        int increment = 0;
                        bool endLoop = false;
                        
                        switch (ReadBits(2, ref bitBuffer, ref inputPos, input))
                        {
                            case 0:
                                if (!ReadBit(ref bitBuffer, ref inputPos, input))
                                {
                                    endLoop = true;
                                    break;
                                }
                                increment = ReadBit(ref bitBuffer, ref inputPos, input) ? 1926 : 1914;
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
                        if (currentPos + 2 >= output.Length)
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
            for (int i = 0; i < output.Length; i += 3)
            {
                if (output[i] == 255 && output[i + 1] == 255 && output[i + 2] == 255)
                {
                    output[i] = lastR;
                    output[i + 1] = lastG;
                    output[i + 2] = lastB;
                }
                else
                {
                    lastR = output[i];
                    lastG = output[i + 1];
                    lastB = output[i + 2];
                }
            }
            
            return output;
        }

        private bool ReadBit(ref int bitBuffer, ref int position, byte[] input)
        {
            bool result = (input[position] & bitBuffer) != 0;
            bitBuffer >>= 1;
            if (bitBuffer == 0)
            {
                bitBuffer = 128;
                position++;
            }
            return result;
        }

        private int ReadBits(int count, ref int bitBuffer, ref int position, byte[] input)
        {
            int result = 0;
            int mask = 1 << (count - 1);

            while (count > 0)
            {
                if ((input[position] & bitBuffer) != 0)
                    result |= mask;

                mask >>= 1;
                bitBuffer >>= 1;

                if (bitBuffer == 0)
                {
                    bitBuffer = 128;
                    position++;
                }

                count--;
            }

            return result;
        }
    }

    class Program
    {
        static void Main(string[] args)
        {
            if (args.Length != 2)
            {
                Console.WriteLine("Usage: DCL2BMP <input_directory> <output_directory>");
                return;
            }

            var decoder = new DclDecoder();
            try
            {
                decoder.ConvertDirectory(args[0], args[1]);
                Console.WriteLine("Conversion completed successfully.");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error: {ex.Message}");
            }
        }
    }
}
