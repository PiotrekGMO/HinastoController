<?php
declare(strict_types=1);

namespace Hinasto\Services;

use Hinasto\Core\HttpException;
use Hinasto\Core\Request;
use Hinasto\Repositories\DeviceRepository;

/** Authenticates an already approved controller by its id and secret (the same headers as the sync). */
final class DeviceAuth
{
    public static function approved(Request $request): array
    {
        $id = $request->header('X-Device-Id');
        $secret = $request->header('X-Device-Secret');
        if (!preg_match('/^[A-Za-z0-9._-]{6,64}$/', $id) || !preg_match('/^[A-Za-z0-9]{32,128}$/', $secret)) {
            throw new HttpException(400, 'bad credentials format');
        }
        $device = (new DeviceRepository())->findByDeviceId($id);
        if ($device === null || !hash_equals((string)$device['secret_hash'], hash('sha256', $secret))) {
            throw new HttpException(401, 'bad secret');
        }
        if ($device['status'] !== 'approved') {
            throw new HttpException(403, 'not approved');
        }
        return $device;
    }
}