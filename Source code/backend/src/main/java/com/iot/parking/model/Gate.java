package com.iot.parking.model;

import jakarta.persistence.*;

@Entity
@Table(name = "tblGate")
public class Gate {
	@Id
	@GeneratedValue(strategy = GenerationType.IDENTITY)
	private Long id;
	private String gateId;
	private String action;
	
	public Gate() {}

	public Gate(String gateId, String action) {
		this.gateId = gateId;
		this.action = action;
	}

	public Long getId() {
		return id;
	}

	public void setId(Long id) {
		this.id = id;
	}

	public String getGateId() {
		return gateId;
	}

	public void setGateId(String gateId) {
		this.gateId = gateId;
	}

	public String getAction() {
		return action;
	}

	public void setAction(String action) {
		this.action = action;
	}
}
