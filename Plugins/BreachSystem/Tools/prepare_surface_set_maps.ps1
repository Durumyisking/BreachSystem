param([string]$SourceDirectory = (Join-Path $PSScriptRoot '../SourceArt/SurfaceSet_20261114'))
$ErrorActionPreference = 'Stop'
$sourceRoot = [System.IO.Path]::GetFullPath($SourceDirectory)
$manifest = Get-Content -LiteralPath (Join-Path $sourceRoot 'generation.json') -Raw | ConvertFrom-Json
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing.Common,System.Drawing.Primitives,System.Private.Windows.Core,System.Private.Windows.GdiPlus -TypeDefinition @'
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class BreachSurfaceSetMaps
{
    private static void Save(string path, byte[] pixels, int size)
    {
        using(var image = new Bitmap(size, size, PixelFormat.Format32bppArgb))
        {
            var locked = image.LockBits(new Rectangle(0, 0, size, size), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(pixels, 0, locked.Scan0, pixels.Length);
            image.UnlockBits(locked);
            image.Save(path, ImageFormat.Png);
        }
    }

    public static string Prepare(string input, string output, string kind)
    {
        const int size = 1024;
        bool masked = kind == "DecoWood";
        bool wrap = kind.StartsWith("Wall");
        var pixels = new byte[size * size * 4];
        using(var source = new Bitmap(input))
        using(var image = new Bitmap(size, size, PixelFormat.Format32bppArgb))
        {
            using(var g = Graphics.FromImage(image))
            {
                g.CompositingMode = CompositingMode.SourceCopy;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(source, new Rectangle(0, 0, size, size));
            }
            var locked = image.LockBits(new Rectangle(0, 0, size, size), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(locked.Scan0, pixels, 0, pixels.Length);
            image.UnlockBits(locked);
        }
        var heights = new double[size * size];
        var smooth = new double[heights.Length];
        var normals = new byte[pixels.Length];
        int empty = 0;
        for(int p = 0; p < heights.Length; ++p)
        {
            int i = p * 4;
            if(!masked) pixels[i + 3] = 255;
            double r = pixels[i + 2] / 255.0, g = pixels[i + 1] / 255.0, b = pixels[i] / 255.0;
            double luma = r * .2126 + g * .7152 + b * .0722;
            // Color-derived relief is approximate. Recess pale mortar instead of raising it.
            heights[p] = kind == "WallBrick" ? .7 * Math.Clamp((r - Math.Max(g,b) - .025) * 7, 0, 1) + .15 * luma : luma;
            if(pixels[i + 3] < 128) ++empty;
        }
        for(int y = 0; y < size; ++y)
        for(int x = 0; x < size; ++x)
        {
            int p = y * size + x;
            double sum = 0, weight = 0;
            for(int yy = -1; yy <= 1; ++yy)
            for(int xx = -1; xx <= 1; ++xx)
            {
                int sx = wrap ? (x + xx + size) % size : Math.Clamp(x + xx, 0, size - 1);
                int sy = wrap ? (y + yy + size) % size : Math.Clamp(y + yy, 0, size - 1);
                int q = sy * size + sx;
                if(pixels[q * 4 + 3] < 128) continue;
                double w = (xx == 0 ? 2 : 1) * (yy == 0 ? 2 : 1);
                sum += heights[q] * w; weight += w;
            }
            smooth[p] = weight > 0 ? sum / weight : 0;
        }
        double strength = kind == "WallBrick" ? 3.5 : kind == "DecoWood" ? 2.5 : kind == "WallConcrete" ? 2.0 : 3.0;
        double maxError = 0;
        for(int y = 0; y < size; ++y)
        for(int x = 0; x < size; ++x)
        {
            int p = y * size + x, i = p * 4;
            double nx = 0, ny = 0;
            if(pixels[i + 3] >= 128)
            {
                int l = y * size + (wrap ? (x + size - 1) % size : Math.Max(0, x - 1));
                int r = y * size + (wrap ? (x + 1) % size : Math.Min(size - 1, x + 1));
                int t = (wrap ? (y + size - 1) % size : Math.Max(0, y - 1)) * size + x;
                int b = (wrap ? (y + 1) % size : Math.Min(size - 1, y + 1)) * size + x;
                // Ignore transparent neighbors to avoid a synthetic rim on masked gaps.
                double hl = pixels[l * 4 + 3] >= 128 ? smooth[l] : smooth[p];
                double hr = pixels[r * 4 + 3] >= 128 ? smooth[r] : smooth[p];
                double ht = pixels[t * 4 + 3] >= 128 ? smooth[t] : smooth[p];
                double hb = pixels[b * 4 + 3] >= 128 ? smooth[b] : smooth[p];
                nx = (hl - hr) * strength;
                ny = (hb - ht) * strength;
            }
            double length = Math.Sqrt(nx * nx + ny * ny + 1);
            normals[i] = (byte)Math.Round((1 / length * .5 + .5) * 255);
            normals[i + 1] = (byte)Math.Round((ny / length * .5 + .5) * 255);
            normals[i + 2] = (byte)Math.Round((nx / length * .5 + .5) * 255);
            normals[i + 3] = 255;
            double qx = normals[i + 2] / 127.5 - 1, qy = normals[i + 1] / 127.5 - 1, qz = normals[i] / 127.5 - 1;
            maxError = Math.Max(maxError, Math.Abs(Math.Sqrt(qx*qx + qy*qy + qz*qz) - 1));
        }
        if(masked && (empty == 0 || empty > heights.Length * .5)) throw new Exception("Unexpected wood alpha coverage");
        if(maxError > .015) throw new Exception("Normal normalization failed");
        Save(Path.Combine(output, "T_" + kind + "_BaseColor.png"), pixels, size);
        Save(Path.Combine(output, "T_" + kind + "_Normal.png"), normals, size);
        return String.Format("{0}: {1}x{1}, transparent {2:F3}%, normal length error {3:F5}", kind, size, empty * 100.0 / heights.Length, maxError);
    }
}
'@
$results = @()
foreach ($entry in $manifest.assets) {
    $original = Join-Path $sourceRoot ($entry.key + '_Generated.png')
    Copy-Item -LiteralPath $entry.path -Destination $original -Force
    $results += [BreachSurfaceSetMaps]::Prepare($original, $sourceRoot, $entry.key)
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $sourceRoot 'map-verification.json')
$results
