<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use Hinasto\Core\I18n;
use Hinasto\Core\Request;
use Hinasto\Core\Response;

/** The web app manifest: the paths depend on where the app is served from (domain root or a sub directory). */
final class PwaController
{
    public function manifest(Request $request): Response
    {
        $base = $request->base;
        $icon = static fn(string $file, string $size, string $purpose) => [
            'src' => $base . '/assets/icons/' . $file, 'sizes' => $size, 'type' => 'image/png', 'purpose' => $purpose,
        ];
        $manifest = [
            'name' => 'Hinasto Controller',
            'short_name' => 'Hinasto',
            'description' => t('app.description'),
            'lang' => I18n::lang(),
            'id' => $base . '/',
            'start_url' => $base . '/',
            'scope' => $base . '/',
            'display' => 'standalone',
            'background_color' => '#0d1218',
            'theme_color' => '#0d1218',
            'icons' => [
                $icon('icon-192.png', '192x192', 'any'),
                $icon('icon-512.png', '512x512', 'any'),
                $icon('maskable-512.png', '512x512', 'maskable'),
            ],
        ];
        return new Response(
            (string)json_encode($manifest, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE),
            200,
            ['Content-Type' => 'application/manifest+json; charset=utf-8', 'Cache-Control' => 'no-cache']
        );
    }
}