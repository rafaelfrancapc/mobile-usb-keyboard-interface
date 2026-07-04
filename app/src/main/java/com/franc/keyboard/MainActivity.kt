package com.franc.keyboard

import android.os.Bundle
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
        // IP fixo da Raspberry Pi na rede que aparece sobre o cabo USB
        // (ver instruções de configuração de IP estático em usb0/rndis).
        private const val RPI_HOST = "192.168.3.3"
        private const val RPI_PORT = 8765
        private const val CONNECT_TIMEOUT_MS = 2000
        private const val RETRY_DELAY_MS = 2000L
    }

    private var inputBuffer = StringBuilder()
    private lateinit var tvDisplay: TextView

    @Volatile private var socket: Socket? = null
    @Volatile private var outputStream: OutputStream? = null
    @Volatile private var conectando = false
    @Volatile private var ativo = true

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        tvDisplay = findViewById(R.id.tvDisplay)

        setupNumericButtons()

        findViewById<AppCompatButton>(R.id.btnDelete).setOnClickListener { deletarUltimoDigito() }
        findViewById<AppCompatButton>(R.id.btnEnter).setOnClickListener { processarEEnviarDados() }

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
    }

    /**
     * Garante que há uma tentativa de conexão em andamento com a Raspberry Pi.
     * Fica tentando reconectar em background até conseguir, sem travar a UI.
     */
    private fun garantirConexao() {
        if (socket != null || conectando) return
        conectando = true
        thread {
            while (ativo && socket == null) {
                try {
                    val s = Socket()
                    s.connect(InetSocketAddress(RPI_HOST, RPI_PORT), CONNECT_TIMEOUT_MS)
                    socket = s
                    outputStream = s.getOutputStream()
                    runOnUiThread {
                        Toast.makeText(this, "Conectado à catraca", Toast.LENGTH_SHORT).show()
                    }
                } catch (e: IOException) {
                    Thread.sleep(RETRY_DELAY_MS)
                }
            }
            conectando = false
        }
    }

    private fun fecharConexao() {
        try {
            outputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            // ignorado: conexão já pode estar quebrada
        }
        outputStream = null
        socket = null
    }

    private fun setupNumericButtons() {
        val buttonIds = arrayOf(
            R.id.btn0, R.id.btn1, R.id.btn2, R.id.btn3, R.id.btn4,
            R.id.btn5, R.id.btn6, R.id.btn7, R.id.btn8, R.id.btn9
        )
        for (id in buttonIds) {
            findViewById<AppCompatButton>(id).setOnClickListener { view ->
                val botaoClicado = view as AppCompatButton
                adicionarAoBuffer(botaoClicado.text.toString())
            }
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