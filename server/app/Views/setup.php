<main class="wrap narrow">
  <div class="brand big">
    <img src="<?= $e($base) ?>/assets/logo.svg" alt="" width="48" height="48">
    <div class="brand-text"><strong>Hinasto</strong><span>Controller</span></div>
  </div>

  <form method="post" action="<?= $e($base) ?>/setup" class="card form" autocomplete="off">
    <input type="hidden" name="lang" value="<?= $e($lang) ?>">
    <h1><?= $t('setup.title') ?></h1>
    <p class="muted"><?= $t('setup.intro') ?></p>

    <fieldset>
      <legend><?= $t('setup.lang_legend') ?></legend>
      <div class="ranges">
        <?php foreach ($languages as $code => $name): ?>
          <a class="chip<?= $code === $lang ? ' sel' : '' ?>" href="<?= $e($base) ?>/setup?lang=<?= $e($code) ?>" hreflang="<?= $e($code) ?>"><?= $e($name) ?></a>
        <?php endforeach; ?>
      </div>
      <p class="muted small"><?= $t('setup.lang_hint') ?></p>
    </fieldset>

    <ul class="checks">
      <?php foreach ($checks as $c): ?>
        <li class="<?= $c['ok'] ? 'ok' : 'bad' ?>">
          <span class="mark"><?= $c['ok'] ? '✓' : '✗' ?></span>
          <span><?= $e($c['label']) ?><?php if (!$c['ok']): ?><small><?= $e($c['hint']) ?></small><?php endif; ?></span>
        </li>
      <?php endforeach; ?>
    </ul>

    <?php if ($error !== ''): ?>
      <div class="flash" role="alert"><?= $e($error) ?></div>
    <?php endif; ?>

    <fieldset>
      <legend><?= $t('setup.db_legend') ?></legend>
      <label class="field"><span><?= $t('setup.db_host') ?></span><input name="db_host" value="<?= $e($v['db_host'] ?? 'localhost') ?>" required></label>
      <label class="field"><span><?= $t('setup.db_name') ?></span><input name="db_name" value="<?= $e($v['db_name'] ?? '') ?>" required></label>
      <label class="field"><span><?= $t('setup.db_user') ?></span><input name="db_user" value="<?= $e($v['db_user'] ?? '') ?>" required></label>
      <label class="field"><span><?= $t('setup.db_pass') ?></span><input name="db_pass" type="password"></label>
      <label class="field">
        <span><?= $t('setup.db_prefix') ?></span>
        <input name="db_prefix" value="<?= $e($v['db_prefix'] ?? 'hn_') ?>" maxlength="20" pattern="[A-Za-z0-9_]*">
        <small><?= $t('setup.db_prefix_hint') ?></small>
      </label>
    </fieldset>

    <fieldset>
      <legend><?= $t('setup.root_legend') ?></legend>
      <p class="muted small"><?= $t('setup.root_hint') ?></p>
      <label class="field"><span><?= $t('setup.root_user') ?></span><input name="root_user" value="<?= $e($v['root_user'] ?? '') ?>" placeholder="<?= $t('setup.root_user_ph') ?>"></label>
      <label class="field"><span><?= $t('setup.root_pass') ?></span><input name="root_pass" type="password"></label>
    </fieldset>

    <fieldset>
      <legend><?= $t('setup.admin_legend') ?></legend>
      <label class="field"><span><?= $t('setup.admin_pass') ?></span><input name="admin_password" type="password" minlength="10" required></label>
      <label class="field"><span><?= $t('setup.admin_pass2') ?></span><input name="admin_password2" type="password" minlength="10" required></label>
    </fieldset>

    <fieldset>
      <legend><?= $t('setup.ctl_legend') ?></legend>
      <label class="field">
        <span><?= $t('setup.enroll_key') ?></span>
        <input name="enroll_key" value="<?= $e($v['enroll_key'] ?? '') ?>" placeholder="<?= $t('setup.enroll_key_ph') ?>">
        <small><?= $t('setup.enroll_key_hint') ?></small>
      </label>
      <label class="field"><span><?= $t('setup.timezone') ?></span><input name="timezone" value="<?= $e($v['timezone'] ?? '') ?>"></label>
    </fieldset>

    <button class="btn primary wide" type="submit"><?= $t('setup.submit') ?></button>
  </form>
</main>
