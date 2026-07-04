package com.franc.keyboard

import android.app.AlertDialog
import android.content.Context
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.text.InputType
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.AppCompatButton
import java.io.IOException
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    companion object {
        // IP padrão da Raspberry Pi, usado apenas se o usuário nunca configurou
        // nada pelo menu oculto (long-press no rótulo "IDENTIFICAÇÃO DIGITADA").
        private const val DEFAULT_RPI_HOST = "192.168.42.222"
        private const val RPI_PORT = 8765
        private const val CONNECT_TIMEOUT_MS = 2000
        private const val RETRY_DELAY_MS = 2000L

        private const val PREFS_NAME = "catraca_config"
        private const val PREF_RPI_HOST = "rpi_host"
    }

    private var inputBuffer = StringBuilder()
    private lateinit var tvDisplay: TextView

    @Volatile private var serverSocket: java.net.ServerSocket? = null
    @Volatile private var socket: Socket? = null
    @Volatile private var outputStream: OutputStream? = null
    @Volatile private var conectando = false
    @Volatile private var ativo = true

    private lateinit var prefs: android.content.SharedPreferences
    private var rpiHost: String = DEFAULT_RPI_HOST

    private lateinit var audioManager: AudioManager
    private lateinit var vibrator: Vibrator

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        rpiHost = prefs.getString(PREF_RPI_HOST, DEFAULT_RPI_HOST) ?: DEFAULT_RPI_HOST

        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        audioManager.loadSoundEffects()
        vibrator = criarVibrator()

        tvDisplay = findViewById(R.id.tvDisplay)

        setupNumericButtons()
        setupBotaoDelete()
        setupBotaoEnter()
        setupMenuOcultoDeIp()

        garantirConexao()
    }

    override fun onResume() {
        super.onResume()
        ativo = true
        garantirConexao()
    }

    override fun onDestroy() {
        super.onDestroy()
        ativo = false
        fecharConexao()
        audioManager.unloadSoundEffects()
    }

    // ---------------------------------------------------------------------
    // Feedback tátil e sonoro
    // ---------------------------------------------------------------------

    private fun criarVibrator(): Vibrator {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val manager = getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
            manager.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
        }
    }

    private fun vibrar(duracaoMs: Long) {
        if (!vibrator.hasVibrator()) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            vibrator.vibrate(VibrationEffect.createOneShot(duracaoMs, VibrationEffect.DEFAULT_AMPLITUDE))
        } else {
            @Suppress("DEPRECATION")
            vibrator.vibrate(duracaoMs)
        }
    }

    private fun feedbackDigito() {
        vibrar(15)
        audioManager.playSoundEffect(AudioManager.FX_KEYPRESS_STANDARD)
    }

    private fun feedbackDelete() {
        vibrar(15)
        audioManager.playSoundEffect(AudioManager.FX_KEYPRESS_DELETE)
    }

    private fun feedbackLimparTudo() {
        // vibração mais longa para diferenciar claramente de um delete simples
        vibrar(45)
        audioManager.playSoundEffect(AudioManager.FX_KEYPRESS_DELETE)
    }

    private fun feedbackEnter() {
        vibrar(25)
        audioManager.playSoundEffect(AudioManager.FX_KEYPRESS_RETURN)
    }

    // ---------------------------------------------------------------------
    // Conexão com a Raspberry Pi
    // ---------------------------------------------------------------------

    /**
     * Garante que há uma tentativa de conexão em andamento com a Raspberry Pi.
     * Fica tentando reconectar em background até conseguir, sem travar a UI.
     */
    private fun garantirConexao() {
        if (socket != null || conectando) return
        conectando = true
        thread {
            try {
                // Em vez de conectar a um IP externo, o tablet vira o SERVIDOR
                // Ele abre a porta localmente e espera o computador se conectar a ele
                if (serverSocket == null) {
                    serverSocket = java.net.ServerSocket(RPI_PORT)
                }

                while (ativo && socket == null) {
                    try {
                        // Fica aguardando a conexão vinda do cabo USB (via comando ADB)
                        val s = serverSocket?.accept()
                        if (s != null) {
                            socket = s
                            outputStream = s.getOutputStream()
                            runOnUiThread {
                                Toast.makeText(this, "Computador conectado via USB!", Toast.LENGTH_SHORT).show()
                            }
                        }
                    } catch (e: IOException) {
                        if (!ativo) break
                        Thread.sleep(RETRY_DELAY_MS)
                    }
                }
            } catch (e: IOException) {
                runOnUiThread {
                    Toast.makeText(this, "Erro ao abrir porta $RPI_PORT", Toast.LENGTH_LONG).show()
                }
            } finally {
                conectando = false
            }
        }
    }

    private fun fecharConexao() {
        try {
            outputStream?.close()
            socket?.close()
        } catch (e: Exception) { /* ignorado */ }

        // Não feche o serverSocket aqui se quiser que ele continue ouvindo novas conexões após quedas
        outputStream = null
        socket = null
    }

    // ---------------------------------------------------------------------
    // Menu oculto: alterar IP da Raspberry Pi
    // ---------------------------------------------------------------------

    private fun setupMenuOcultoDeIp() {
        findViewById<TextView>(R.id.tvLabelDisplay).setOnLongClickListener {
            abrirMenuDeConfiguracao()
            true
        }
    }

    private fun abrirMenuDeConfiguracao() {
        val input = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(rpiHost)
            setSelection(text.length)
        }

        val paddingPx = (20 * resources.displayMetrics.density).toInt()
        val container = FrameLayout(this).apply {
            setPadding(paddingPx, paddingPx, paddingPx, 0)
            addView(input)
        }

        AlertDialog.Builder(this)
            .setTitle("Configuração de rede")
            .setMessage("Endereço IP da Raspberry Pi:")
            .setView(container)
            .setPositiveButton("Salvar") { _, _ ->
                val novoIp = input.text.toString().trim()
                if (novoIp.isNotEmpty() && novoIp != rpiHost) {
                    rpiHost = novoIp
                    prefs.edit().putString(PREF_RPI_HOST, novoIp).apply()
                    Toast.makeText(this, "IP salvo: $novoIp", Toast.LENGTH_SHORT).show()
                    fecharConexao()
                    garantirConexao()
                }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ---------------------------------------------------------------------
    // Botões numéricos, DEL e ENTER
    // ---------------------------------------------------------------------

    private fun setupNumericButtons() {
        val buttonIds = arrayOf(
            R.id.btn0, R.id.btn1, R.id.btn2, R.id.btn3, R.id.btn4,
            R.id.btn5, R.id.btn6, R.id.btn7, R.id.btn8, R.id.btn9
        )
        for (id in buttonIds) {
            findViewById<AppCompatButton>(id).setOnClickListener { view ->
                val botaoClicado = view as AppCompatButton
                feedbackDigito()
                adicionarAoBuffer(botaoClicado.text.toString())
            }
        }
    }

    private fun setupBotaoDelete() {
        val btnDelete = findViewById<AppCompatButton>(R.id.btnDelete)

        // toque simples: apaga só o último dígito
        btnDelete.setOnClickListener {
            feedbackDelete()
            deletarUltimoDigito()
        }

        // pressionar e segurar: apaga tudo
        btnDelete.setOnLongClickListener {
            feedbackLimparTudo()
            limparTudo()
            true // consome o evento, evita disparar o onClick de delete simples ao soltar
        }
    }

    private fun setupBotaoEnter() {
        findViewById<AppCompatButton>(R.id.btnEnter).setOnClickListener {
            feedbackEnter()
            processarEEnviarDados()
        }
    }

    private fun adicionarAoBuffer(digito: String) {
        if (inputBuffer.length < 11) {
            inputBuffer.append(digito)
            atualizarDisplay()
        }
    }

    private fun deletarUltimoDigito() {
        if (inputBuffer.isNotEmpty()) {
            inputBuffer.deleteCharAt(inputBuffer.length - 1)
            atualizarDisplay()
        }
    }

    private fun limparTudo() {
        inputBuffer.clear()
        atualizarDisplay()
    }

    private fun atualizarDisplay() {
        tvDisplay.text = inputBuffer.toString()
    }

    private fun processarEEnviarDados() {
        val dadoParaEnviar = inputBuffer.toString()
        if (dadoParaEnviar.isEmpty()) return

        val stream = outputStream
        if (stream == null) {
            Toast.makeText(this, "Sem conexão com a catraca", Toast.LENGTH_SHORT).show()
            garantirConexao()
            return
        }

        val linha = "$dadoParaEnviar\n"
        thread {
            try {
                stream.write(linha.toByteArray(Charsets.UTF_8))
                stream.flush()
                runOnUiThread {
                    inputBuffer.clear()
                    atualizarDisplay()
                }
            } catch (e: IOException) {
                runOnUiThread {
                    Toast.makeText(this, "Erro ao enviar: conexão perdida", Toast.LENGTH_SHORT).show()
                }
                fecharConexao()
                garantirConexao()
            }
        }
    }
}