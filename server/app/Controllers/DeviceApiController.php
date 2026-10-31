<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use Hinasto\Core\Config;
use Hinasto\Core\HttpException;
use Hinasto\Core\Request;
use Hinasto\Core\Response;
use Hinasto\Core\Text;
use Hinasto\Repositories\CommandRepository;
use Hinasto\Repositories\DeviceRepository;
use Hinasto\Repositories\FirmwareRepository;
use Hinasto\Repositories\LogRepository;
use Hinasto\Repositories\ReadingRepository;
use Hinasto\Repositories\RunRepository;
use Hinasto\Repositories\StepRepository;
use Hinasto\Services\Maintenance;
use Hinasto\Services\StateNormalizer;
use Throwable;

/** API used by the controllers (ESP32), see README for the message format. */
final class DeviceApiController
{
    private const MAX_PENDING = 20;
    private const MAX_PENDING_PER_IP = 5;

    public function sync(Request $request): Response
    {
        $id = $request->header('X-Device-Id');
        $secret = $request->header('X-Device-Secret');
        if (!preg_match('/^[A-Za-z0-9._-]{6,64}$/', $id) || !preg_match('/^[A-Za-z0-9]{32,128}$/', $secret)) {
            throw new HttpException(400, 'bad credentials format');
        }
        $hash = hash('sha256', $secret);
        $in = $request->json();
        $fw = Text::cut(Text::scalar($in['fw'] ?? ''), 40);

        $devices = new DeviceRepository();
        $device = $devices->findByDeviceId($id);

        if ($device === null) {  // first contact: wait for approval in the panel
            $key = (string)Config::get('enroll_key', '');
            if ($key !== '' && !hash_equals($key, $request->header('X-Enroll-Key'))) {
                throw new HttpException(403, 'enrollment not allowed');
            }
            $devices->dropStalePending();  // controllers that gave up do not keep the places
            if ($devices->countPending() >= self::MAX_PENDING || $devices->countPending($request->ip()) >= self::MAX_PENDING_PER_IP) {
                throw new HttpException(429, 'too many pending controllers');
            }
            $devices->createPending($id, $hash, $fw, $request->ip());
            return Response::json(['status' => 'pending', 'retry' => 10]);
        }
        if (!hash_equals((string)$device['secret_hash'], $hash)) {
            throw new HttpException(401, 'bad secret');
        }

        $deviceId = (int)$device['id'];
        $devices->touch($deviceId, $request->ip(), $fw);
        if ($device['status'] === 'pending') {
            return Response::json(['status' => 'pending', 'retry' => 10]);
        }
        if ($device['status'] === 'rejected') {
            return Response::json(['status' => 'rejected', 'retry' => 600]);
        }

        // What matters for a heater comes first: the acknowledgements and the commands. Storing the telemetry comes
        // after and can fail (a value the database refuses, a full disk) without keeping a command, power off
        // included, from reaching the controller.
        $commands = new CommandRepository();
        $this->safely(function () use ($commands, $deviceId, $in): void {
            foreach (array_slice(is_array($in['acks'] ?? null) ? $in['acks'] : [], 0, 20) as $ack) {
                if (is_array($ack) && isset($ack['id']) && is_numeric($ack['id'])) {
                    $commands->acknowledge($deviceId, (int)$ack['id'], !empty($ack['ok']), Text::scalar($ack['msg'] ?? ''));
                }
            }
        });
        $commands->expireOld();
        $answer = ['status' => 'approved', 'retry' => 3, 'commands' => $commands->takeQueued($deviceId)];

        $state = null;
        $this->safely(function () use ($devices, $deviceId, $in, &$state): void {
            $normalizer = new StateNormalizer();
            $state = $normalizer->normalize($in);
            $devices->saveState($deviceId, $state);
            $this->safely(fn() => (new ReadingRepository())->addIfDue($deviceId, $normalizer->readings($state)));
            $this->safely(fn() => (new RunRepository())->observe($deviceId, $state['heaters']));
            $this->safely(fn() => (new StepRepository())->observe($deviceId, $state['heaters']));
        });
        $this->safely(function () use ($deviceId, $in): void {
            $lines = [];
            foreach (array_slice(is_array($in['log'] ?? null) ? $in['log'] : [], 0, 50) as $line) {
                $lines[] = Text::scalar(is_array($line) ? ($line['m'] ?? '') : $line);
            }
            (new LogRepository())->add($deviceId, $lines);
        });
        $this->safely(fn() => Maintenance::runIfDue());

        // a controller updates only within its own language; firmware from before languages existed reports none and is Polish
        $lang = FirmwareRepository::offerLang($state['lang'] ?? null);
        $release = Config::get('auto_update', true) && $lang !== null ? (new FirmwareRepository())->latest($lang) : null;
        if ($release !== null && ctype_digit($fw) && (int)$release['version'] > (int)$fw) {  // only newer, and only for a controller that reports a number
            $this->safely(fn() => FirmwareRepository::ensureMirror($release));  // controllers on the first versions can only take a static file
            $answer['update'] = ['version' => (int)$release['version'], 'size' => (int)$release['size'], 'md5' => $release['md5'], 'file' => 'firmware-' . $release['lang']];
        }
        return Response::json($answer);
    }

    /** Runs a step whose failure must not fail the whole request: it is logged and the request goes on. */
    private function safely(callable $step): void
    {
        try {
            $step();
        } catch (Throwable $e) {
            error_log('hinasto: sync step failed: ' . $e->getMessage() . ' @ ' . $e->getFile() . ':' . $e->getLine());
        }
    }
}
