/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/renderer/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        neon: {
          red: '#FF0040',
          'red-light': '#FF3366',
          'red-dark': '#CC0033',
          glow: 'rgba(255, 0, 64, 0.5)',
        },
        dark: {
          bg: '#0A0A0F',
          surface: '#121218',
          elevated: '#1A1A24',
          border: '#2A2A35',
        }
      },
      boxShadow: {
        'neon-sm': '0 0 10px rgba(255, 0, 64, 0.3)',
        'neon-md': '0 0 20px rgba(255, 0, 64, 0.4)',
        'neon-lg': '0 0 30px rgba(255, 0, 64, 0.5)',
        'neon-xl': '0 0 40px rgba(255, 0, 64, 0.6)',
      },
      animation: {
        'pulse-glow': 'pulse-glow 2s ease-in-out infinite',
        'float': 'float 6s ease-in-out infinite',
      },
      keyframes: {
        'pulse-glow': {
          '0%, 100%': { opacity: '1' },
          '50%': { opacity: '0.5' },
        },
        'float': {
          '0%, 100%': { transform: 'translateY(0px)' },
          '50%': { transform: 'translateY(-20px)' },
        }
      }
    },
  },
  plugins: [],
}
