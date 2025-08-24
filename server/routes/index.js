// server/routes/index.js
const path = require('path');
const express = require('express');
const router = express.Router();

// Serve the static HTML as the homepage
router.get('/', (req, res) => {
  res.setHeader('Content-Type', 'text/html; charset=utf-8');
  res.sendFile(path.join(__dirname, '../public/index.html'));
});

router.get('/version', (req, res) => res.send('0.0.0'));

module.exports = router;
