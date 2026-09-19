#!/usr/bin/env python3
"""
Wadi Enhancement Tool for RDR2-style Terrain
Enhances dry riverbeds (wadis) on heightmap to look realistic
"""

import numpy as np
from PIL import Image
import cv2
from scipy import ndimage
import argparse
from pathlib import Path

class WadiEnhancer:
    def __init__(self, heightmap_path, output_path=None):
        """Initialize with heightmap image"""
        self.heightmap_path = Path(heightmap_path)
        self.output_path = Path(output_path) if output_path else self.heightmap_path.parent / f"{self.heightmap_path.stem}_wadis.png"

        # Load heightmap
        print(f"[WadiEnhancer] Loading heightmap: {self.heightmap_path}")
        img = Image.open(self.heightmap_path)
        self.original = np.array(img, dtype=np.float32) / 255.0  # Normalize to [0, 1]
        self.height_data = self.original.copy()

        print(f"[WadiEnhancer] Heightmap size: {self.height_data.shape}")
        print(f"[WadiEnhancer] Height range: {self.height_data.min():.3f} to {self.height_data.max():.3f}")

    def find_valleys(self, kernel_size=31, threshold=0.15):
        """Identify valley regions where wadis should be"""
        print(f"\n[Valleys] Finding valley regions (kernel={kernel_size})...")

        # Apply morphological closing to find local minima
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (kernel_size, kernel_size))
        dilated = cv2.dilate(self.height_data, kernel, iterations=1)

        # Valleys are where terrain is significantly lower than surroundings
        valley_mask = (dilated - self.height_data) > threshold

        print(f"[Valleys] Valley coverage: {valley_mask.sum() / valley_mask.size * 100:.1f}%")
        return valley_mask

    def enhance_wadi_channels(self, valley_mask, depth=0.08, smoothness=5):
        """Deepen wadi channels to create realistic dry riverbed appearance"""
        print(f"\n[Channels] Enhancing wadi channels (depth={depth})...")

        # Find gradient direction (flow direction)
        gy, gx = np.gradient(self.height_data)

        # Create channel mask by eroding valleys
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (smoothness, smoothness))
        channel_mask = cv2.erode(valley_mask.astype(np.uint8), kernel, iterations=2).astype(bool)

        # Deepen channels gradually based on valley strength
        valley_strength = (valley_mask.astype(np.float32) *
                          np.sqrt(gx**2 + gy**2) / np.sqrt(gx**2 + gy**2).max())

        # Apply channel deepening
        self.height_data[channel_mask] -= depth * valley_strength[channel_mask]
        self.height_data = np.clip(self.height_data, 0, 1)

        print(f"[Channels] Channels enhanced")

    def add_meandering_patterns(self, amplitude=0.03, frequency=0.05, valley_mask=None):
        """Add realistic meandering curves to wadis"""
        print(f"\n[Meanders] Adding meandering patterns...")

        # Create sinusoidal meandering along the heightmap
        h, w = self.height_data.shape

        # Generate multiple frequency components for natural look
        for freq_mult in [1.0, 0.5, 0.25]:
            y_coords = np.arange(h)
            x_coords = np.arange(w)

            # Create meandering offset
            meander_offset = amplitude * np.sin(y_coords[:, np.newaxis] * frequency * freq_mult / 100) * \
                           np.sin(x_coords[np.newaxis, :] * frequency * freq_mult / 100)

            # Apply only to valley areas if mask provided
            if valley_mask is not None:
                meander_offset *= valley_mask.astype(np.float32)

            # Smoothly reduce channel in meandering areas
            self.height_data += meander_offset * 0.02

        self.height_data = np.clip(self.height_data, 0, 1)
        print(f"[Meanders] Meanders added")

    def erode_wadi_banks(self, iterations=2):
        """Create natural erosion at wadi banks"""
        print(f"\n[Erosion] Applying wadi bank erosion...")

        # Find steep edges (wadi banks)
        gy, gx = np.gradient(self.height_data)
        gradient_mag = np.sqrt(gx**2 + gy**2)
        bank_mask = gradient_mag > np.percentile(gradient_mag, 85)

        # Smooth banks slightly for natural appearance
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        eroded = cv2.erode(bank_mask.astype(np.uint8), kernel, iterations=iterations)

        # Gently lower banks
        self.height_data[eroded.astype(bool)] *= 0.98
        print(f"[Erosion] Bank erosion applied")

    def enhance_wadi_density(self, min_size=50):
        """Increase wadi feature density for RDR2-like appearance"""
        print(f"\n[Density] Enhancing wadi density...")

        # Find all valley-like features at different scales
        for scale in [15, 25, 35]:
            kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (scale, scale))
            dilated = cv2.dilate(self.height_data, kernel, iterations=1)
            valley_at_scale = (dilated - self.height_data) > 0.05

            # Deepen features at this scale
            self.height_data[valley_at_scale] *= 0.96

        self.height_data = np.clip(self.height_data, 0, 1)
        print(f"[Density] Wadi density enhanced")

    def smooth_transitions(self, radius=3):
        """Smooth edges for natural transitions"""
        print(f"\n[Smoothing] Smoothing wadi transitions...")

        # Apply gentle Gaussian blur to smooth harsh transitions
        # Ensure radius is odd for OpenCV
        ksize = radius if radius % 2 == 1 else radius + 1
        self.height_data = cv2.GaussianBlur(self.height_data, (ksize, ksize), 0)
        self.height_data = np.clip(self.height_data, 0, 1)
        print(f"[Smoothing] Transitions smoothed")

    def enhance(self):
        """Run full wadi enhancement pipeline"""
        print("\n" + "="*60)
        print("WADI ENHANCEMENT PIPELINE")
        print("="*60)

        # Step 1: Find valleys
        valley_mask = self.find_valleys(kernel_size=31, threshold=0.12)

        # Step 2: Deepen channels
        self.enhance_wadi_channels(valley_mask, depth=0.06)

        # Step 3: Add meandering
        self.add_meandering_patterns(amplitude=0.025, frequency=0.04, valley_mask=valley_mask)

        # Step 4: Erode banks
        self.erode_wadi_banks(iterations=1)

        # Step 5: Enhance density at multiple scales
        self.enhance_wadi_density()

        # Step 6: Smooth transitions
        self.smooth_transitions(radius=2)

        # Final quality check
        print(f"\n[Quality] Final height range: {self.height_data.min():.3f} to {self.height_data.max():.3f}")
        print(f"[Quality] Height preservation: {(self.height_data >= self.original).sum() / self.height_data.size * 100:.1f}% of terrain preserved")

    def save(self):
        """Save enhanced heightmap"""
        print(f"\n[Save] Saving to: {self.output_path}")

        # Convert back to 0-255 range
        output_data = (self.height_data * 255).astype(np.uint8)

        # Save as PNG
        output_image = Image.fromarray(output_data, mode='L')
        output_image.save(self.output_path)

        print(f"[Save] [OK] Enhanced heightmap saved")
        print(f"[Save] Output: {self.output_path}")

        # Create a comparison visualization
        self._save_comparison()

    def _save_comparison(self):
        """Save before/after comparison"""
        comparison_path = self.output_path.parent / f"{self.output_path.stem}_comparison.png"

        # Create side-by-side comparison
        orig_8bit = (self.original * 255).astype(np.uint8)
        enhanced_8bit = (self.height_data * 255).astype(np.uint8)

        # Stack horizontally
        comparison = np.hstack([orig_8bit, enhanced_8bit])

        comparison_img = Image.fromarray(comparison, mode='L')
        comparison_img.save(comparison_path)
        print(f"[Compare] [OK] Comparison saved: {comparison_path}")


def main():
    parser = argparse.ArgumentParser(description="Enhance wadis on RDR2-style terrain heightmap")
    parser.add_argument("heightmap", help="Path to heightmap.png")
    parser.add_argument("-o", "--output", help="Output path (default: heightmap_wadis.png)")
    parser.add_argument("--depth", type=float, default=0.06, help="Wadi channel depth (0-0.1)")
    parser.add_argument("--amplitude", type=float, default=0.025, help="Meandering amplitude")
    parser.add_argument("--no-backup", action="store_true", help="Don't create backup")

    args = parser.parse_args()

    # Verify input exists
    if not Path(args.heightmap).exists():
        print(f"ERROR: Heightmap not found: {args.heightmap}")
        return 1

    # Create backup if requested
    if not args.no_backup:
        backup_path = Path(args.heightmap).parent / f"{Path(args.heightmap).stem}_backup.png"
        if not backup_path.exists():
            print(f"[Backup] Creating backup: {backup_path}")
            import shutil
            shutil.copy(args.heightmap, backup_path)

    # Run enhancement
    enhancer = WadiEnhancer(args.heightmap, args.output)
    enhancer.enhance()
    enhancer.save()

    print("\n" + "="*60)
    print("[SUCCESS] WADI ENHANCEMENT COMPLETE")
    print("="*60)
    print(f"\nYour enhanced heightmap is ready!")
    print(f"Output: {enhancer.output_path}")
    print(f"\nTo use in the game:")
    print(f"  1. Backup the original: assets/terrain/heightmap.png")
    print(f"  2. Copy the enhanced version over it")
    print(f"  3. Rebuild and test in-game")

    return 0


if __name__ == "__main__":
    exit(main())
