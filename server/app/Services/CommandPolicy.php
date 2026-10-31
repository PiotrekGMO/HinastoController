<?php
declare(strict_types=1);

namespace Hinasto\Services;

use Hinasto\Core\Config;
use Hinasto\Core\HttpException;

/** Decides whether a panel command makes sense for the last reported state. The controller checks it again. */
final class CommandPolicy
{
    public const ACTIONS = ['on', 'off', 'level', 'temp', 'mode', 'smart'];

    /**
     * @param array $device row with seen_ago, state_ago, state_json
     * @return array{heater:int, action:string, value:int}
     */
    public function validate(array $input, array $device): array
    {
        $action = (string)($input['action'] ?? '');
        $heater = (int)($input['heater'] ?? 0);
        $value = (int)($input['value'] ?? 0);
        $confirmed = !empty($input['confirm']);

        if (!in_array($action, self::ACTIONS, true) || $heater < 0 || $heater > 1) {
            throw new HttpException(400, t('err.bad_command'));
        }
        if ($device['seen_ago'] === null || (int)$device['seen_ago'] > (int)Config::get('online_seconds', 20)) {
            throw new HttpException(409, t('err.offline'));
        }
        $state = $device['state_json'] ? json_decode($device['state_json'], true) : null;
        $h = null;
        foreach (($state['heaters'] ?? []) as $x) {
            if ((int)$x['i'] === $heater) {
                $h = $x;
            }
        }
        if ($h === null || $device['state_ago'] === null || (int)$device['state_ago'] > 30) {
            throw new HttpException(409, t('err.no_state'));
        }

        $run = $h['run'];
        switch ($action) {
            case 'on':
                $this->needConfirm($confirmed);
                if ($run !== 'off') {
                    throw new HttpException(409, t('err.on_only_off'));
                }
                break;
            case 'off':
                $this->needConfirm($confirmed);
                break;
            case 'level':
                $this->range($value, 1, 6, t('err.range_level'));
                $this->needRunning($run);
                if (!$h['manual']) {
                    throw new HttpException(409, t('err.is_auto'));
                }
                break;
            case 'temp':
                $this->range($value, 6, 35, t('err.range_temp'));
                $this->needRunning($run);
                if ($h['manual']) {
                    throw new HttpException(409, t('err.is_manual'));
                }
                break;
            case 'smart':  // 0 = off, 6-35 = on / new target; the controller starts and stops the heater by itself
                if ($value !== 0) {
                    $this->needConfirm($confirmed);
                    $this->range($value, 6, 35, t('err.range_temp'));
                    if (empty($state['env'])) {
                        throw new HttpException(409, t('err.smart_sensor'));
                    }
                }
                break;
            case 'mode':
                $this->needConfirm($confirmed);
                $this->range($value, 0, 1, t('err.range_mode'));
                $this->needRunning($run);
                break;
        }
        return ['heater' => $heater, 'action' => $action, 'value' => $value];
    }

    private function needConfirm(bool $ok): void
    {
        if (!$ok) {
            throw new HttpException(400, t('err.confirm'));
        }
    }

    private function needRunning(string $run): void
    {
        if ($run !== 'on') {
            throw new HttpException(409, t('err.not_running'));
        }
    }

    private function range(int $v, int $lo, int $hi, string $message): void
    {
        if ($v < $lo || $v > $hi) {
            throw new HttpException(400, $message);
        }
    }
}
