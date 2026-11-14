param([Parameter(Mandatory=$true)][string]$ApprovedImage)

$ErrorActionPreference = 'Stop'

$sourceArt = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../SourceArt/Decoration'))
New-Item -ItemType Directory -Path $sourceArt -Force | Out-Null
Copy-Item -LiteralPath $ApprovedImage -Destination (Join-Path $sourceArt 'WoodFibers_Approved.png') -Force
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing.Common,System.Drawing.Primitives,System.Private.Windows.Core,System.Private.Windows.GdiPlus -TypeDefinition @'
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class BreachWoodFiberMaps
{
    private static void Save(string path, byte[] data, int size)
    {
        using (var image = new Bitmap(size, size, PixelFormat.Format32bppArgb))
        {
            var locked = image.LockBits(new Rectangle(0, 0, size, size), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(data, 0, locked.Scan0, data.Length);
            image.UnlockBits(locked);
            image.Save(path, ImageFormat.Png);
        }
    }

    public static string Prepare(string input, string output)
    {
        const int size = 1024;
        var pixels = new byte[size * size * 4];
        using (var original = new Bitmap(input))
        using (var image = new Bitmap(size, size, PixelFormat.Format32bppArgb))
        {
            using (var graphics = Graphics.FromImage(image))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.DrawImage(original, new Rectangle(0, 0, size, size));
            }
            var locked = image.LockBits(new Rectangle(0, 0, size, size), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(locked.Scan0, pixels, 0, pixels.Length);
            image.UnlockBits(locked);
        }
        var masks = new byte[pixels.Length];
        var normal = new byte[pixels.Length];
        var height = new double[size * size];
        var empty = new int[4];
        for (int p = 0; p < height.Length; ++p)
        {
            int i = p * 4;
            height[p] = (pixels[i + 2] * .2126 + pixels[i + 1] * .7152 + pixels[i] * .0722) / 255;
            // BGRA: B=opacity, G=AO, R=roughness. The mask is the approved alpha, without a V gradient.
            masks[i] = pixels[i + 3];
            masks[i + 1] = (byte)(255 * (.90 + .10 * height[p]));
            masks[i + 2] = (byte)(255 * (.85 + .10 * (1 - height[p])));
            masks[i + 3] = 255;
            if (pixels[i + 3] < 128)
            {
                ++empty[(p / size) / (size / 4)];
            }
        }
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                int p = y * size + x, i = p * 4;
                int left = y * size + Math.Max(0, x - 1), right = y * size + Math.Min(size - 1, x + 1);
                int top = Math.Max(0, y - 1) * size + x, bottom = Math.Min(size - 1, y + 1) * size + x;
                // Avoid manufacturing a normal ridge around a transparent hole.
                double l = pixels[left * 4 + 3] >= 128 ? height[left] : height[p];
                double r = pixels[right * 4 + 3] >= 128 ? height[right] : height[p];
                double t = pixels[top * 4 + 3] >= 128 ? height[top] : height[p];
                double b = pixels[bottom * 4 + 3] >= 128 ? height[bottom] : height[p];
                double nx = (l - r) * 1.5, ny = (t - b) * 1.5;
                double length = Math.Sqrt(nx * nx + ny * ny + 1);
                normal[i] = (byte)((1 / length * .5 + .5) * 255);
                normal[i + 1] = (byte)((ny / length * .5 + .5) * 255);
                normal[i + 2] = (byte)((nx / length * .5 + .5) * 255);
                normal[i + 3] = 255;
            }
        }
        Save(Path.Combine(output, "T_Deco_Wood_BaseColor.png"), pixels, size);
        Save(Path.Combine(output, "T_Deco_Wood_Normal.png"), normal, size);
        Save(Path.Combine(output, "T_Deco_Wood_Masks.png"), masks, size);
        return String.Format("1024x1024; mask empty by quarter top-to-bottom: {0:F2}%, {1:F2}%, {2:F2}%, {3:F2}%", empty[0] / 2621.44, empty[1] / 2621.44, empty[2] / 2621.44, empty[3] / 2621.44);
    }
}
'@
[BreachWoodFiberMaps]::Prepare((Join-Path $sourceArt 'WoodFibers_Approved.png'), $sourceArt)
