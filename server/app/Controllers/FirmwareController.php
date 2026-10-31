<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use Hinasto\Core\HttpException;
use Hinasto\Core\Request;
use Hinasto\Core\Version;
use Hinasto\Core\Response;
use Hinasto\Repositories\DeviceRepository;
use Hinasto\Repositories\FirmwareRepository;
use Hinasto\Services\DeviceAuth;
use Hinasto\Services\FirmwareImage;

/** Firmware releases: upload and overview in the panel, download by the controllers. */
final class FirmwareController
{
    /** panel: what is published (per language) and which controller runs what */
    public function overview(Request $request): Response
    {
        $firmware = new FirmwareRepository();
        $devices = [];
        foreach ((new DeviceRepository())->listForPanel() as $d) {
            if ($d['status'] === 'approved') {
                $state = $d['state_json'] ? json_decode($d['state_json'], true) : [];
                $devices[] = [
                    'id' => (int)$d['id'],
                    'name' => $d['name'] !== '' ? $d['name'] : $d['device_id'],
                    'fw' => $d['fw'],
                    'lang' => FirmwareRepository::normalizeLang((string)($state['lang'] ?? '')),
                ];
            }
        }
        return Response::json([
            'latest' => array_values($firmware->latestByLang()),
            'releases' => $firmware->recent(),
            'devices' => $devices,
            'server' => Version::info(),
        ]);
    }

    /** panel: a .bin from the build; the newest version of its language is what the controllers of that language update to */
    public function upload(Request $request): Response
    {
        $file = $_FILES['file'] ?? null;
        if (!is_array($file) || ($file['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK) {
            $err = (int)($file['error'] ?? UPLOAD_ERR_NO_FILE);
            throw new HttpException(400, in_array($err, [UPLOAD_ERR_INI_SIZE, UPLOAD_ERR_FORM_SIZE], true)
                ? t('err.fw_ini_size')
                : t('err.fw_no_file'));
        }
        $info = FirmwareImage::inspect((string)$file['tmp_name']);
        $firmware = new FirmwareRepository();
        $firmware->ensureTable();
        $latest = $firmware->latest($info['lang']);
        if ($latest !== null && $info['version'] < (int)$latest['version']) {
            throw new HttpException(409, t('err.fw_older', ['version' => $info['version'], 'latest' => $latest['version']]));
        }
        if (!is_dir(FirmwareRepository::dir()) && !@mkdir(FirmwareRepository::dir(), 0775, true)) {
            throw new HttpException(500, t('err.fw_dir'));
        }
        $dest = FirmwareRepository::dir() . '/' . $info['version'] . '-' . $info['lang'] . '.bin';
        if (!@move_uploaded_file((string)$file['tmp_name'], $dest)) {
            throw new HttpException(500, t('err.fw_save'));
        }
        $firmware->save($info['version'], $info['lang'], $info['size'], $info['md5'], (string)($file['name'] ?? ''));
        $static = FirmwareRepository::mirror($info['version'], $info['lang']);
        return Response::json(['ok' => true, 'version' => $info['version'], 'lang' => $info['lang'], 'size' => $info['size'], 'static' => $static]);
    }

    /**
     * controller: the image of the version the sync offered. GET /api/v1/firmware-<lang>?v=<n> (the static copy
     * normally answers before PHP gets here), GET /api/v1/firmware?v=<n> for controllers from before languages existed.
     */
    public function download(Request $request): Response
    {
        DeviceAuth::approved($request);
        $lang = preg_match('#/firmware-([a-z]{2})$#', $request->path, $m) ? FirmwareRepository::normalizeLang($m[1]) : 'pl';
        $firmware = new FirmwareRepository();
        $release = $firmware->find((int)$request->query('v'), $lang) ?? $firmware->latest($lang);
        $path = $release ? FirmwareRepository::path((int)$release['version'], $lang) : '';
        if ($release === null || !is_file($path)) {
            throw new HttpException(404, 'no firmware');
        }
        return Response::file($path, 'hinasto-' . $release['version'] . '-' . strtoupper($lang) . '.bin');
    }
}
