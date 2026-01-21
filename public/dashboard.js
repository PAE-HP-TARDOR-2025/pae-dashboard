class Dashboard {
    constructor() {
        this.apiBase = '/api';
        this.sensors = new Map();
        this.chart = null;
        this.theme = localStorage.getItem('dashboard-theme') || 'light';
        this.init();
    }

    init() {
        this.applyTheme();
        this.initChart();
        this.loadData();
        this.setupEventListeners();
        this.startAutoRefresh(3000);
    }

    applyTheme() {
        document.body.className = this.theme === 'dark' ? 'dark-theme' : '';
        const icon = document.querySelector('#themeToggle i');
        icon.className = this.theme === 'dark' ? 'fas fa-sun' : 'fas fa-moon';
        localStorage.setItem('dashboard-theme', this.theme);
    }

    setupEventListeners() {
        // Toggle tema
        document.getElementById('themeToggle').addEventListener('click', () => {
            this.theme = this.theme === 'light' ? 'dark' : 'light';
            this.applyTheme();
        });

        // Botones de control
        document.getElementById('refreshSensors').addEventListener('click', () => this.loadData());
        document.getElementById('refreshChart').addEventListener('click', () => this.refreshChart());
        document.getElementById('clearData').addEventListener('click', () => this.clearData());
        document.getElementById('closeEmergency').addEventListener('click', () => {
            document.getElementById('emergencyAlert').style.display = 'none';
        });
    }

    async loadData() {
        try {
            const [sensorsRes, statsRes] = await Promise.all([
                fetch(`${this.apiBase}/sensors`),
                fetch(`${this.apiBase}/stats`)
            ]);

            if (!sensorsRes.ok || !statsRes.ok) {
                throw new Error('Error de conexión');
            }

            const sensors = await sensorsRes.json();
            const stats = await statsRes.json();

            this.updateStatus(true);
            this.updateSensors(sensors);
            this.updateStats(stats);
            this.updateChart(sensors);
            this.updateRecentData();

        } catch (error) {
            console.error('Error cargando datos:', error);
            this.updateStatus(false);
        }
    }

    updateSensors(sensorData) {
        const container = document.getElementById('sensorsContainer');
        
        if (!sensorData || sensorData.length === 0) {
            container.innerHTML = `
                <div style="grid-column: 1/-1; text-align: center; padding: 2rem; color: var(--color-text-secondary);">
                    <i class="fas fa-wifi-slash" style="font-size: 2rem; margin-bottom: 1rem; opacity: 0.5;"></i>
                    <p>Esperando datos de sensores...</p>
                </div>
            `;
            return;
        }

        container.innerHTML = '';
        let emergencyDetected = false;

        sensorData.forEach(sensor => {
            const card = document.createElement('div');
            card.className = `sensor-card ${sensor.emergency ? 'emergency' : ''}`;
            
            const timeAgo = this.getTimeAgo(sensor.lastUpdate);
            const statusClass = sensor.emergency ? 'status-emergency' : 
                              sensor.active ? 'status-active' : 'status-inactive';
            const statusText = sensor.emergency ? 'EMERGENCIA' : 
                              sensor.active ? 'ACTIVO' : 'INACTIVO';
            
            card.innerHTML = `
                <div class="sensor-header">
                    <div class="sensor-name">${sensor.name}</div>
                    <div class="sensor-id">${sensor.id}</div>
                </div>
                <div class="sensor-value ${sensor.emergency ? 'emergency' : ''}">
                    ${sensor.value.toFixed(1)} <span style="font-size: 1rem;">${sensor.unit}</span>
                </div>
                <div class="sensor-footer">
                    <span>${timeAgo}</span>
                    <span class="sensor-status ${statusClass}">${statusText}</span>
                </div>
            `;

            container.appendChild(card);
            
            // No mostrar alerta popup, solo marcar que hay emergencia
            if (sensor.emergency) {
                emergencyDetected = true;
            }
        });

        // Actualizar contador de sensores
        document.getElementById('sensorCount').textContent = 
            `${sensorData.filter(s => s.active).length}/${sensorData.length}`;
    }

    updateStats(stats) {
        document.getElementById('statActiveSensors').textContent = stats.activeSensors;
        document.getElementById('statTotalData').textContent = stats.totalData;
        document.getElementById('statEmergencies').textContent = stats.totalEmergencies;
        
        // Calcular uptime aproximado
        const uptime = stats.totalSensors > 0 ? 
            Math.round((stats.activeSensors / stats.totalSensors) * 100) : 100;
        document.getElementById('statUptime').textContent = `${uptime}%`;
        
        // Actualizar última actualización
        const now = new Date();
        document.getElementById('lastUpdate').textContent = 
            now.toLocaleTimeString('es-ES', { hour12: false });
    }

    async updateRecentData() {
        try {
            const response = await fetch(`${this.apiBase}/items`);
            if (!response.ok) throw new Error('Error cargando datos recientes');
            
            const items = await response.json();
            const tbody = document.getElementById('recentDataBody');
            
            if (!items || items.length === 0) {
                tbody.innerHTML = `
                    <tr>
                        <td colspan="4" style="text-align: center; padding: 2rem; color: var(--color-text-secondary);">
                            No hay datos disponibles
                        </td>
                    </tr>
                `;
                return;
            }

            // Mostrar los 10 más recientes
            const recentItems = items.slice(0, 10);
            tbody.innerHTML = '';

            recentItems.forEach(item => {
                const row = document.createElement('tr');
                const time = new Date(item.timestamp).toLocaleTimeString('es-ES', {
                    hour: '2-digit',
                    minute: '2-digit'
                });
                
                const value = item.body.value || item.body.distance || 0;
                const unit = item.body.unit || '';
                const name = item.body.name || `Sensor ${item.id}`;
                
                row.innerHTML = `
                    <td>
                        <div style="font-weight: 500;">${name}</div>
                        <div style="font-size: 0.8rem; color: var(--color-text-secondary);">${item.id}</div>
                    </td>
                    <td style="font-weight: 600;">
                        ${value.toFixed(1)} ${unit}
                    </td>
                    <td>
                        <span class="sensor-status ${item.emergency ? 'status-emergency' : 'status-active'}">
                            ${item.emergency ? 'EMERGENCIA' : 'NORMAL'}
                        </span>
                    </td>
                    <td style="font-size: 0.85rem; color: var(--color-text-secondary);">
                        ${time}
                    </td>
                `;
                
                tbody.appendChild(row);
            });

        } catch (error) {
            console.error('Error cargando datos recientes:', error);
        }
    }

    initChart() {
        const ctx = document.getElementById('comparisonChart').getContext('2d');
        
        this.chart = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [],
                datasets: [{
                    label: 'Valor',
                    data: [],
                    backgroundColor: 'rgba(59, 130, 246, 0.5)',
                    borderColor: 'rgb(59, 130, 246)',
                    borderWidth: 1,
                    borderRadius: 4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: false
                    },
                    tooltip: {
                        callbacks: {
                            label: (context) => {
                                return `${context.parsed.y.toFixed(1)} unidades`;
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        grid: {
                            color: 'rgba(0, 0, 0, 0.1)'
                        },
                        ticks: {
                            color: 'var(--color-text-secondary)'
                        }
                    },
                    x: {
                        grid: {
                            color: 'rgba(0, 0, 0, 0.1)'
                        },
                        ticks: {
                            color: 'var(--color-text-secondary)'
                        }
                    }
                }
            }
        });
    }

    updateChart(sensorData) {
        if (!this.chart || !sensorData) return;
        
        const labels = sensorData.map(s => s.name.length > 15 ? s.name.substring(0, 15) + '...' : s.name);
        const data = sensorData.map(s => s.value);
        const colors = sensorData.map(s => s.emergency ? 'rgba(239, 68, 68, 0.7)' : 'rgba(59, 130, 246, 0.5)');
        const borderColors = sensorData.map(s => s.emergency ? 'rgb(239, 68, 68)' : 'rgb(59, 130, 246)');
        
        this.chart.data.labels = labels;
        this.chart.data.datasets[0].data = data;
        this.chart.data.datasets[0].backgroundColor = colors;
        this.chart.data.datasets[0].borderColor = borderColors;
        this.chart.update();
    }

    updateStatus(connected) {
        const dot = document.querySelector('.status-dot');
        const text = document.querySelector('#statusIndicator span:last-child');
        const badge = document.getElementById('statusIndicator');
        
        if (connected) {
            dot.className = 'status-dot connected';
            text.textContent = 'Conectado';
            badge.style.color = '';
        } else {
            dot.className = 'status-dot';
            text.textContent = 'Desconectado';
            badge.style.color = 'var(--color-danger)';
        }
    }

    getTimeAgo(timestamp) {
        const now = new Date();
        const past = new Date(timestamp);
        const diff = now - past;
        
        const seconds = Math.floor(diff / 1000);
        const minutes = Math.floor(seconds / 60);
        const hours = Math.floor(minutes / 60);
        
        if (seconds < 60) return 'Ahora mismo';
        if (minutes < 60) return `Hace ${minutes} min`;
        if (hours < 24) return `Hace ${hours} h`;
        return `Hace ${Math.floor(hours / 24)} d`;
    }

    showEmergencyAlert(sensor) {
        const alert = document.getElementById('emergencyAlert');
        const title = document.getElementById('emergencyTitle');
        const message = document.getElementById('emergencyMessage');
        
        title.textContent = `¡EMERGENCIA en ${sensor.name}!`;
        message.textContent = `Valor: ${sensor.value}${sensor.unit} - Requiere atención inmediata`;
        alert.style.display = 'flex';
        
        // No ocultar automáticamente, dejar que el usuario cierre manualmente
        // setTimeout(() => {
        //     alert.style.display = 'none';
        // }, 10000);
    }

    refreshChart() {
        this.loadData();
        
        Toastify({
            text: "Gráfico actualizado",
            duration: 2000,
            gravity: "top",
            position: "right",
            backgroundColor: "linear-gradient(to right, #00b09b, #96c93d)"
        }).showToast();
    }

    async clearData() {
        if (!confirm('¿Estás seguro de que deseas eliminar todos los datos?')) {
            return;
        }
        
        try {
            const response = await fetch(`${this.apiBase}/items`, {
                method: 'DELETE'
            });
            
            if (response.ok) {
                this.loadData();
                
                Toastify({
                    text: "✅ Todos los datos han sido eliminados",
                    duration: 3000,
                    gravity: "top",
                    position: "right",
                    backgroundColor: "linear-gradient(to right, #00b09b, #96c93d)"
                }).showToast();
            }
        } catch (error) {
            console.error('Error eliminando datos:', error);
        }
    }

    startAutoRefresh(interval) {
        setInterval(() => this.loadData(), interval);
    }
}

// Inicializar cuando el DOM esté listo
document.addEventListener('DOMContentLoaded', () => {
    window.dashboard = new Dashboard();
});