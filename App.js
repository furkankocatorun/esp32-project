import React from "react";
import Sidebar from "./components/Sidebar";
import Header from "./components/Header";
import Dashboard from "./components/Dashboard";
import "./App.css";

function App() {
  return (
    <div className="app">
      <Sidebar />
      <div className="main">
        <Header />
        <Dashboard />
      </div>
    </div>
  );
}

export default App;
