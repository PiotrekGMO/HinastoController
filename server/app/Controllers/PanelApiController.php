<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use Hinasto\Core\Auth;
use Hinasto\Core\Config;
use Hinasto\Core\HttpException;
use Hinasto\Core\Request;
use Hinasto\Core\Response;
use Hinasto\Core\Text;
use Hinasto\Core\View;
use Hinasto\Repositories\CommandRepository;
use Hinasto\Repositories\DeviceRepository;
use Hinasto\Repositories\LogRepository;
use Hinasto\Repositories\ReadingRepository;
use Hinasto\Repositories\RunRepository;
use Hinasto\Services\CommandPolicy;

/** JSON API of the panel (all routes behind the 'auth' guard, POST ones also behind 'csrf'). */
final class PanelApiController
{
    public function state(Request $request): Response
    {
        $commands = new CommandRepository();
        $commands->expireOld();
        $online = (int)Config::get('online_seconds', 20);

        $devices = [];
        $pending = [];
        $rejected = [];
        foreach ((new DeviceRepository())->listForPanel() as $row) {
            if ($row['status'] === 'rejected') {  // kept visible so that it can be taken back or deleted
                $rejected[] = ['id' => (int)$row['id'], 'device_id' => $row['device_id'], 'ip' => $row['ip'], 'first_seen' => $row['first_seen']];
                continue;
            }
            if ($row['status'] === 'pending') {
                $pending[] = [
                    'id' => (int)$row['id'],
                    'device_id' => $row['device_id'],
                    'ip' => $row['ip'],
                    'fw' => $row['fw'],
                    'first_seen' => $row['first_seen'],
                ];
                continue;
            }
            $seen = $row['seen_ago'] === null ? null : (int)$row['seen_ago'];
            $devices[] = [
                'id' => (int)$row['id'],
                'device_id' => $row['device_id'],
                'name' => $row['name'] !== '' ? $row['name'] : $row['device_id'],
                'fw' => $row['fw'],
                'ip' => $row['ip'],
                'seen_ago' => $seen,
                'online' => $seen !== null && $seen <= $online,
                'state_ago' => $row['state_ago'] === null ? null : (int)$row['state_ago'],
                'state' => $row['state_json'] ? json_decode($row['state_json'], true) : null,
                'commands' => $commands->recent((int)$row['id'], 3),
                'last_smart' => $commands->lastSmartTarget((int)$row['id']),
            ];
        }
        return Response::json(['csrf' => Auth::csrf(), 'build' => View::assetVersion(), 'devices' => $devices, 'pending' => $pending, 'rejected' => $rejected]);
    }

    /** What a new controller asks for in its setup portal: the key (the server address is the one the panel runs on). */
    public function enroll(Request $request): Response
    {
        return Response::json(['key' => (string)Config::get('enroll_key', '')]);
    }

    /** approve / reject / rename / delete */
    public function device(Request $request): Response
    {
        $in = $request->json(2048);
        $id = (int)($in['id'] ?? 0);
        $name = trim(Text::cut(Text::scalar($in['name'] ?? ''), 80));
        $devices = new DeviceRepository();
        $device = $devices->find($id);
        if ($device === null) {
            throw new HttpException(404, t('err.unknown_device'));
        }
        switch ((string)($in['op'] ?? '')) {
            case 'approve':
                if (!in_array($device['status'], ['pending', 'rejected'], true)) {  // a rejected one may be taken back
                    throw new HttpException(409, t('err.not_pending'));
                }
                $devices->approve($id, $name !== '' ? $name : t('join.default_name'));
                break;
            case 'reject':
                if ($device['status'] !== 'pending') {  // an approved controller is removed with delete, not by a reject
                    throw new HttpException(409, t('err.not_pending'));
                }
                $devices->reject($id);
                break;
            case 'rename':
                if ($name === '') {
                    throw new HttpException(400, t('err.name_required'));
                }
                $devices->rename($id, $name);
                break;
            case 'move':
                $devices->move($id, (int)($in['dir'] ?? 0) < 0 ? -1 : 1);
                break;
            case 'delete':  // it may announce itself again and wait for approval
                $devices->delete($id);
                break;
            default:
                throw new HttpException(400, t('err.unknown_op'));
        }
        return Response::json(['ok' => true]);
    }

    public function command(Request $request): Response
    {
        $in = $request->json(2048);
        $device = (new DeviceRepository())->findApproved((int)($in['device'] ?? 0));
        if ($device === null) {
            throw new HttpException(404, t('err.unknown_device'));
        }
        $cmd = (new CommandPolicy())->validate($in, $device);
        $id = (new CommandRepository())->queueExclusive((int)$device['id'], $cmd['heater'], $cmd['action'], $cmd['value']);
        if ($id === null) {
            throw new HttpException(409, t('err.prev_commands'));
        }
        return Response::json(['ok' => true, 'id' => $id]);
    }

    public function log(Request $request): Response
    {
        $device = (int)$request->query('device', '0');
        $after = (int)$request->query('after', '0');
        $lines = (new LogRepository())->since($device, $after);
        return Response::json(['lines' => $lines, 'last' => $lines ? (int)end($lines)['id'] : $after]);
    }

    /** Working periods for the chart and the totals of work time. */
    public function runs(Request $request): Response
    {
        $device = (int)$request->query('device', '0');
        $heater = (int)$request->query('heater', '0');
        $hours = max(1, min(72, (int)$request->query('hours', '12')));
        $now = time();
        $midnight = (new \DateTimeImmutable('today'))->getTimestamp();  // today in the time zone of the config
        $runs = new RunRepository();
        return Response::json([
            'intervals' => $runs->intervals($device, $heater, $now - $hours * 3600, $now),
            'totals' => $runs->totals($device, $heater, $now, $midnight),
        ]);
    }

    public function history(Request $request): Response
    {
        $kind = $request->query('kind', 'env_t');
        if (!in_array($kind, ReadingRepository::KINDS, true)) {
            throw new HttpException(400, t('err.unknown_kind'));
        }
        $hours = max(1, min(72, (int)$request->query('hours', '24')));
        $points = (new ReadingRepository())->series((int)$request->query('device', '0'), $kind, (int)$request->query('heater', '0'), $hours);
        return Response::json(['points' => $points]);
    }
}
