<main class="wrap narrow">
  <div class="brand big">
    <img src="<?= $e($base) ?>/assets/logo.svg" alt="" width="48" height="48">
    <div class="brand-text"><strong>Hinasto</strong><span>Controller</span></div>
  </div>

  <section class="card form">
    <h1><?= $t('done.title') ?></h1>
    <p><?= $th('done.text') ?></p>

    <div class="key">
      <span class="muted small"><?= $t('done.key_label') ?></span>
      <code><?= $e($enrollKey) ?></code>
    </div>

    <p class="muted small"><?= $th('done.key_note') ?></p>
    <a class="btn primary wide" href="<?= $e($base) ?>/"><?= $t('done.go') ?></a>
  </section>
</main>
